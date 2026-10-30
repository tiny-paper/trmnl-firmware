// Loads and runs a native app (apps.h, docs/apps.md) with Espressif's elf_loader, and supervises it:
// feeds it touch bar and serial key events, ends it on a long press, and goes back to TRMNL.
#ifdef BOARD_TRMNL_X

#include <LittleFS.h>
#include <app_elf_check.h>
#include <app_keys.h>
#include <app_path.h>
#include <app_resume.h>
#include <apps.h>
#include <bl.h>
#include <cached_images.h>
#include <epd_driver.h>
#include <esp_elf.h>
#include <esp_heap_caps.h>
#include <filesystem.h>
#include <fonts.h>
#include <globals.h>
#include <iqs323_task.h>
#include <preferences_persistence.h>
#include <trmnl_log.h>

#include <algorithm>

#include "app_host.h"
#include "webshare.h"

#define APP_POLL_MS       20
#define APP_LONG_PRESS_MS 1500
#define APP_KEY_IDLE_MS   50 // after a lone ESC: the Escape key, not the start of a sequence

// ---- listing ----

int apps_list(char names[][32], char paths[][APPS_PATH_MAX], int max) {
  File dir = LittleFS.open(APPS_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  while (File entry = dir.openNextFile()) {
    char path[APPS_PATH_MAX];
    snprintf(path, sizeof(path), APPS_DIR "/%s", entry.name());
    bool is_dir = entry.isDirectory();
    entry.close();
    char name[32];
    if (is_dir || !app_path_name(path, name, sizeof(name))) continue;
    if (n == max) {
      Log_info("Apps: more than %d, %s not listed", max, path);
      continue;
    }
    strcpy(names[n], name);
    strcpy(paths[n], path);
    n++;
  }
  dir.close();
  // Sort by name, the order of the file system's directory is not
  for (int i = 1; i < n; i++) {
    for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
      std::swap_ranges(names[j], names[j] + 32, names[j - 1]);
      std::swap_ranges(paths[j], paths[j] + APPS_PATH_MAX, paths[j - 1]);
    }
  }
  return n;
}

// ---- back to TRMNL ----

// Shows why the app stopped, then sleeps; the next wake is TRMNL's as usual
static void app_error_and_sleep(const char *name, const char *what) {
  Log_error("Apps: %s: %s", name, what);
  bbep.setMode(BB_MODE_1BPP);
  bbep.fillScreen(BBEP_WHITE);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  bbep.setFont(Roboto_Black_24);
  bbep.drawString("App stopped", bbep.width() / 6, bbep.height() / 3);
  bbep.setFont(Inter_18);
  bbep.drawString(name, bbep.width() / 6, bbep.height() / 3 + 110);
  bbep.drawString(what, bbep.width() / 6, bbep.height() / 3 + 180);
  bbep.drawString("TRMNL continues at the next refresh.", bbep.width() / 6, bbep.height() / 3 + 280);
  bbep.fullUpdate(CLEAR_FAST, false);
  goToSleep();
}

// ---- input ----

// The touch bar, per channel (0 left, 1 middle, 2 right): a tap on release, an exit request once
// a touch has been held for APP_LONG_PRESS_MS
typedef struct {
  bool down;
  uint32_t since;
  bool long_press;
} app_touch_t;

static app_touch_t touch[3];
static app_key_decoder_t keys;
static uint32_t last_key_byte;

static void post(const app_event_t &ev) {
  if (xQueueSend(app_host.events, &ev, 0) != pdTRUE) Log_info("Apps: event queue full, event dropped");
}

static void post_tap(int channel) {
  app_event_t ev = {};
  ev.type = APP_EVENT_TOUCH;
  ev.zone = (uint8_t)channel;
  ev.gesture = APP_GESTURE_TAP;
  post(ev);
}

// Polls the touch bar; true on a new long press
static bool pump_touch(void) {
  touchbar_snapshot_t snap;
  iqs323_task_read_snapshot(&snap);
  uint8_t starts = iqs323_task_take_touch_starts();
  const bool now_down[3] = {snap.ch0_touch, snap.ch1_touch, snap.ch2_touch};
  bool long_press = false;
  uint32_t now = millis();
  for (int ch = 0; ch < 3; ch++) {
    app_touch_t &t = touch[ch];
    bool started = starts & (1 << ch);
    if (!t.down && (now_down[ch] || started)) {
      t = {true, now, false};
    }
    if (!t.down) continue;
    if (now_down[ch]) {
      if (!t.long_press && now - t.since >= APP_LONG_PRESS_MS) {
        t.long_press = true;
        long_press = true;
      }
      continue;
    }
    // released (or a tap too short for the snapshot to show it down)
    if (!t.long_press) post_tap(ch);
    t.down = false;
  }
  return long_press;
}

// Reads the serial port: a terminal's keys
static void pump_keys(void) {
  app_event_t ev;
  while (Serial.available() > 0) {
    int b = Serial.read();
    if (b < 0) break;
    last_key_byte = millis();
    if (!app_key_feed(&keys, (uint8_t)b, &ev)) continue;
    post(ev);
    ev.type = APP_EVENT_KEY_UP;
    post(ev);
  }
  if (keys.len && millis() - last_key_byte >= APP_KEY_IDLE_MS && app_key_idle(&keys, &ev)) {
    post(ev);
    ev.type = APP_EVENT_KEY_UP;
    post(ev);
  }
}

// ---- running ----

typedef int (*app_entry_t)(const app_api_t *api);

static app_entry_t app_entry;

void app_host_finish(int code) {
  app_host.code = code;
  xSemaphoreGive(app_host.done);
  vTaskDelete(nullptr);
}

static void app_task(void *arg) {
  (void)arg;
  app_host_finish(app_entry(app_api_table()));
}

// Reads and checks the ELF, then loads it; returns an error text, or nullptr with *elf loaded
static const char *app_load(const char *path, esp_elf_t *elf, app_info_t *info) {
  static char error[64];
  if (!LittleFS.exists(path)) return "file not found";
  uint8_t *buf = nullptr;
  size_t len = filesystem_read_and_allocate(path, &buf);
  if (!len || !buf) {
    free(buf);
    return "cannot read the file (empty, or out of memory)";
  }
  size_t free_mem = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  app_elf_result_t check = app_elf_check(buf, len, free_mem, info);
  if (check != APP_ELF_OK) {
    free(buf);
    snprintf(error, sizeof(error), "%s", app_elf_result_str(check));
    return error;
  }
  esp_elf_init(elf);
  int rc = esp_elf_relocate(elf, buf);
  free(buf);
  if (rc != 0) {
    esp_elf_deinit(elf);
    snprintf(error, sizeof(error), rc == -ENOSYS ? "needs a function the firmware lacks" : "cannot load it (%d)", rc);
    return error;
  }
  return nullptr;
}

// Waits for the app to end, pumping its input. Returns false if it was told to exit and did not
// within APP_EXIT_GRACE_MS.
static bool app_supervise(PreferencesPersistence &persistence) {
  uint32_t started = millis();
  bool marked_started = false;
  uint32_t exit_asked = 0;
  while (xSemaphoreTake(app_host.done, pdMS_TO_TICKS(APP_POLL_MS)) != pdTRUE) {
    if (!marked_started && millis() - started >= APP_RESUME_STARTED_MS) {
      app_resume_started(persistence);
      marked_started = true;
    }
    pump_keys();
    if (pump_touch() && !exit_asked) {
      Log_info("Apps: long press - asking %s to exit", app_host.name);
      app_resume_ended(persistence);
      app_event_t ev = {};
      ev.type = APP_EVENT_EXIT;
      xQueueReset(app_host.events);
      post(ev);
      exit_asked = millis();
    }
    if (exit_asked && millis() - exit_asked >= APP_EXIT_GRACE_MS) return false;
  }
  return true;
}

void apps_run(const char *path) {
  PreferencesPersistence persistence(preferences);
  memset(&app_host, 0, sizeof(app_host));
  if (!app_path_name(path, app_host.name, sizeof(app_host.name))) {
    app_resume_ended(persistence);
    app_error_and_sleep(path, "not an app path (/apps/<name>.elf)");
  }
  snprintf(app_host.dir, sizeof(app_host.dir), APPS_DATA_DIR "/%s", app_host.name);
  Log_info("Apps: starting %s", path);
  app_resume_starting(persistence, path);

  static esp_elf_t elf;
  app_info_t info;
  const char *error = app_load(path, &elf, &info);
  if (error) {
    app_resume_ended(persistence);
    app_error_and_sleep(app_host.name, error);
  }
  if (!LittleFS.exists(APPS_DATA_DIR)) LittleFS.mkdir(APPS_DATA_DIR);
  if (!LittleFS.exists(app_host.dir)) LittleFS.mkdir(app_host.dir);

  app_host.events = xQueueCreate(APP_HOST_EVENT_QUEUE, sizeof(app_event_t));
  app_host.done = xSemaphoreCreateBinary();
  app_entry = (app_entry_t)elf.entry;
  app_key_decoder_init(&keys);
  Serial.begin(115200); // the keys of a terminal on the USB serial port
  iqs323_task_set_streaming_mode(true); // fresh touch data while the app runs
  iqs323_task_take_touch_starts();      // the tap that started it
  // A touch still down (the tap that chose the app, or the one that woke the device) is not the app's
  touchbar_snapshot_t snap;
  iqs323_task_read_snapshot(&snap);
  const bool down[3] = {snap.ch0_touch, snap.ch1_touch, snap.ch2_touch};
  for (int ch = 0; ch < 3; ch++) {
    touch[ch] = {down[ch], millis(), down[ch]};
  }

  TaskHandle_t task = nullptr;
  if (!app_host.events || !app_host.done ||
      xTaskCreatePinnedToCore(app_task, "app", info.stack_size, nullptr, 1, &task, 1) != pdPASS) {
    esp_elf_deinit(&elf);
    app_resume_ended(persistence);
    app_error_and_sleep(app_host.name, "not enough memory for its stack");
  }
  Log_info("Apps: %s (%s) running, %lu bytes of stack", info.name, path, (unsigned long)info.stack_size);

  if (!app_supervise(persistence)) {
    // Its task may hold a lock or be anywhere in its code: only a restart cleans that up
    Log_error("Apps: %s did not exit within %d ms - restarting into TRMNL", app_host.name, APP_EXIT_GRACE_MS);
    app_resume_ended(persistence);
    delay(100);
    ESP.restart();
  }
  int code = app_host.code;
  Log_info("Apps: %s ended with %d", app_host.name, code);
  iqs323_task_set_streaming_mode(false);
  webshare_stop(); // the page never outlives the app
  app_host_close_files();
  esp_elf_deinit(&elf);
  vQueueDelete(app_host.events);
  vSemaphoreDelete(app_host.done);
  app_resume_ended(persistence);
  if (code != 0) {
    char what[48];
    snprintf(what, sizeof(what), "ended with error %d", code);
    app_error_and_sleep(app_host.name, what);
  }
  show_last_image_and_sleep();
}

void apps_resume_if_due(void) {
  PreferencesPersistence persistence(preferences);
  esp_reset_reason_t reason = esp_reset_reason();
  bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
                 reason == ESP_RST_WDT;
  char path[APPS_PATH_MAX];
  if (!app_resume_take(persistence, crashed, path, sizeof(path))) {
    if (crashed) Log_info("Apps: reset by a crash (%d), no app resumed", reason);
    return;
  }
  Log_info("Apps: resuming %s", path);
  apps_run(path);
}

#endif // BOARD_TRMNL_X
