// Loads and runs a native app (apps.h, docs/apps.md) with Espressif's elf_loader, and supervises it:
// feeds it touch bar and serial key events, ends it on a long press, and goes back to TRMNL.
#ifdef BOARD_TRMNL_X

#include <LittleFS.h>
#include <app_elf_check.h>
#include <app_install.h>
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
#include <qrcodegen.h>
#include <trmnl_log.h>

#include <algorithm>

#include "app_host.h"
#include "ble_keyboard.h"
#include "keyboards.h"
#include "usb_drive.h"
#include "usb_port_role.h"
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

// An app installed over Wi-Fi that fails to start gets its older version back (app_install.h); the text says so
static const char *with_restore_note(Persistence &p, const char *what) {
  static char text[96];
  if (!app_install_failed(p, webshare_fs_root(), app_host.name)) return what;
  snprintf(text, sizeof(text), "%s; the previous version was put back", what);
  return text;
}

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
  if (!app_host.events) return; // a firmware screen (Install via Wi-Fi): no app to take it
  app_host.got_input = true;
  if (xQueueSend(app_host.events, &ev, 0) != pdTRUE) Log_info("Apps: event queue full, event dropped");
}

static void post_tap(int channel) {
  if (ble_keyboard_tap(channel)) return; // the Bluetooth pairing screen's
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
    // A new touch start always begins a new touch: the touch held at touch_reset (the tap that chose the app or
    // screen) may never show its release in the snapshot, and a stale "down" swallowed every later press: no tap,
    // no long press (device report: Install via Wi-Fi and apps could not be left)
    if (started || (!t.down && now_down[ch])) {
      Log_info("Touch: ch%d down%s%s", ch, now_down[ch] ? "" : " (start only)", t.down ? ", the last one's release missed" : "");
      t = {true, now, false};
    }
    if (!t.down) continue;
    if (now_down[ch]) {
      if (!t.long_press && now - t.since >= APP_LONG_PRESS_MS) {
        t.long_press = true;
        long_press = true;
        Log_info("Touch: ch%d long press", ch);
      }
      continue;
    }
    // released (or a tap too short for the snapshot to show it down)
    Log_info("Touch: ch%d up after %lu ms%s", ch, (unsigned long)(now - t.since), t.long_press ? "" : ": tap");
    if (!t.long_press) post_tap(ch);
    t.down = false;
  }
  return long_press;
}

// A touch still down (the tap that chose the app, or the one that woke the device) is not the app's
static void touch_reset(void) {
  touchbar_snapshot_t snap;
  iqs323_task_read_snapshot(&snap);
  const bool down[3] = {snap.ch0_touch, snap.ch1_touch, snap.ch2_touch};
  for (int ch = 0; ch < 3; ch++) {
    touch[ch] = {down[ch], millis(), down[ch]};
  }
}

// "TAPP NORESUME" + newline on the serial port while an app runs: way out without the touch bar (docs/apps.md):
// nothing is resumed after the restart that follows
#define APP_CMD_NORESUME "TAPP NORESUME"
static void serial_command(char c) {
  static char line[24];
  static size_t len;
  if (c != '\n' && c != '\r') {
    if (len + 1 >= sizeof(line)) { // keep the last bytes
      memmove(line, line + 1, len - 1);
      len--;
    }
    line[len++] = c;
    return;
  }
  line[len] = '\0';
  size_t n = len, c_len = sizeof(APP_CMD_NORESUME) - 1;
  len = 0;
  // the end of the line: keys typed before it (no newline in between) do not hide the command
  if (n < c_len || strcmp(line + n - c_len, APP_CMD_NORESUME) != 0) return;
  PreferencesPersistence persistence(preferences);
  app_resume_ended(persistence);
  Log_info("Apps: resume cleared by serial command - restarting into TRMNL");
  delay(100);
  ESP.restart();
}

// Reads the serial port: a terminal's keys
static void pump_keys(void) {
  app_event_t ev;
  while (Serial.available() > 0) {
    int b = Serial.read();
    if (b < 0) break;
    last_key_byte = millis();
    serial_command((char)b);
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
  bool marked_started = false, marked_input = false;
  uint32_t exit_asked = 0;
  while (xSemaphoreTake(app_host.done, pdMS_TO_TICKS(APP_POLL_MS)) != pdTRUE) {
    if (!marked_started && millis() - started >= APP_RESUME_STARTED_MS) {
      app_resume_started(persistence);
      app_install_started(persistence, webshare_fs_root(), app_host.name);
      marked_started = true;
    }
    if (!marked_input && app_host.got_input) {
      app_resume_input(persistence); // a later restart may resume it: the user could reach it
      marked_input = true;
    }
    usb_drive_service();
    if (!usb_drive_busy()) pump_keys(); // the serial port is off while the drive shows
    if (pump_touch() && !exit_asked) {
      Log_info("Apps: long press - asking %s to exit", app_host.name);
      app_resume_ended(persistence);
      app_event_t ev = {};
      ev.type = APP_EVENT_EXIT;
      xQueueReset(app_host.events);
      post(ev);
      exit_asked = millis();
    }
    // An import under way finishes first (a cut would be safe, the journal, but slower to redo)
    if (usb_drive_importing()) exit_asked = exit_asked ? millis() : 0;
    if (exit_asked && millis() - exit_asked >= APP_EXIT_GRACE_MS) return false;
  }
  return true;
}

void apps_run(const char *path) {
  PreferencesPersistence persistence(preferences);
  memset(&app_host, 0, sizeof(app_host));
  if (!app_path_name(path, app_host.name, sizeof(app_host.name))) {
    app_resume_ended(persistence);
    app_error_and_sleep(path, "not an app path (" APPS_DIR "/<name>.elf)");
  }
  snprintf(app_host.dir, sizeof(app_host.dir), APPS_DATA_DIR "/%s", app_host.name);
  Log_info("Apps: starting %s", path);
  app_resume_starting(persistence, path);
  app_install_start(persistence, webshare_fs_root(), app_host.name); // a new version installed over Wi-Fi

  static esp_elf_t elf;
  app_info_t info;
  const char *error = app_load(path, &elf, &info);
  if (error) {
    app_resume_ended(persistence);
    app_error_and_sleep(app_host.name, with_restore_note(persistence, error));
  }
  if (!LittleFS.exists(APPS_DATA_DIR)) LittleFS.mkdir(APPS_DATA_DIR);
  if (!LittleFS.exists(app_host.dir)) LittleFS.mkdir(app_host.dir);
  usb_drive_recover(app_host.dir); // an import a power cut interrupted: finished before the app reads

  app_host.events = xQueueCreate(APP_HOST_EVENT_QUEUE, sizeof(app_event_t));
  app_host.done = xSemaphoreCreateBinary();
  app_host.panel = xSemaphoreCreateMutex();
  app_entry = (app_entry_t)elf.entry;
  app_key_decoder_init(&keys);
  Serial.begin(115200); // the keys of a terminal on the USB serial port
  iqs323_task_set_streaming_mode(true); // fresh touch data while the app runs
  iqs323_task_take_touch_starts();      // the tap that started it
  touch_reset();

  TaskHandle_t task = nullptr;
  if (!app_host.events || !app_host.done || !app_host.panel ||
      xTaskCreatePinnedToCore(app_task, "app", info.stack_size, nullptr, 1, &task, 1) != pdPASS) {
    esp_elf_deinit(&elf);
    app_resume_ended(persistence);
    app_error_and_sleep(app_host.name, with_restore_note(persistence, "not enough memory for its stack"));
  }
  Log_info("Apps: %s (%s) running, %lu bytes of stack", info.name, path, (unsigned long)info.stack_size);
  // For tools/app_addr2line.py: the app's frames in a panic backtrace are entry - e_entry + their ELF address
  Log_info("Apps: %s entry at 0x%08lx", app_host.name, (unsigned long)(uintptr_t)elf.entry);

  if (!app_supervise(persistence)) {
    // Its task may hold a lock or be anywhere in its code: only a restart cleans that up
    Log_error("Apps: %s did not exit within %d ms - restarting into TRMNL", app_host.name, APP_EXIT_GRACE_MS);
    app_resume_ended(persistence);
    delay(100);
    ESP.restart();
  }
  usb_drive_end_for_exit(); // serial back, the computer's changes taken or not, before anything else
  int code = app_host.code;
  Log_info("Apps: %s ended with %d", app_host.name, code);
  keyboards_end(); // the USB port back in device mode before anything else
  usb_port_give(usb_port_role()); // whatever still holds the PHY: serial again (usb_port_role.h)
  iqs323_task_set_streaming_mode(false);
  webshare_stop(); // the page never outlives the app
  app_host_close_files();
  esp_elf_deinit(&elf);
  vQueueDelete(app_host.events);
  vSemaphoreDelete(app_host.done);
  vSemaphoreDelete(app_host.panel);
  app_host.panel = nullptr;
  app_resume_ended(persistence);
  if (code != 0) {
    char what[48];
    snprintf(what, sizeof(what), "ended with error %d", code);
    app_error_and_sleep(app_host.name, with_restore_note(persistence, what));
  }
  app_install_started(persistence, webshare_fs_root(), app_host.name); // it ran and ended well: no .bak needed
  show_last_image_and_sleep();
}

// ---- Install via Wi-Fi ----

#define QR_SCALE 12

// Draws the QR of `text` with its top left at (x, y); false if it does not fit a version-8 code
static bool draw_qr(const char *text, int x, int y) {
  static uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)], code[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
  if (!qrcodegen_encodeText(text, temp, code, qrcodegen_Ecc_LOW, 1, 8, qrcodegen_Mask_AUTO, true)) return false;
  int size = qrcodegen_getSize(code), quiet = 2;
  bbep.fillRect(x, y, (size + 2 * quiet) * QR_SCALE, (size + 2 * quiet) * QR_SCALE, BBEP_WHITE);
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      if (qrcodegen_getModule(code, col, row)) {
        bbep.fillRect(x + (col + quiet) * QR_SCALE, y + (row + quiet) * QR_SCALE, QR_SCALE, QR_SCALE, BBEP_BLACK);
      }
    }
  }
  return true;
}

void apps_install_session(void) {
  int rc = webshare_start_install();
  app_webshare_status_t st;
  webshare_status(&st);
  if (rc != 0 || !st.running) {
    char what[48];
    snprintf(what, sizeof(what), "could not start (error %d)", rc);
    app_error_and_sleep("Install via Wi-Fi", what);
  }
  Log_info("Apps: install page on, %s", st.url);
  const int x = bbep.width() / 12;
  const int y = bbep.height() / 6;
  bbep.setMode(BB_MODE_1BPP);
  bbep.fillScreen(BBEP_WHITE);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  bbep.setFont(Roboto_Black_24);
  bbep.drawString("Install apps via Wi-Fi", x, y);
  char line[80];
  bbep.setFont(Inter_18);
  if (st.mode == APP_WEBSHARE_HOME) {
    snprintf(line, sizeof(line), "Online at %s", st.url);
    bbep.drawString(line, x, y + 110);
  } else {
    snprintf(line, sizeof(line), "No home Wi-Fi: join the network \"%s\"", st.network);
    bbep.drawString(line, x, y + 110);
    snprintf(line, sizeof(line), "then open %s", st.url);
    bbep.drawString(line, x, y + 180);
  }
  snprintf(line, sizeof(line), "Code: %s", st.code);
  bbep.drawString(line, x, y + 290);
  bbep.drawString("Scan the code on the right, or type the code on the page.", x, y + 380);
  bbep.drawString("Long press to stop.", x, y + 480);
  draw_qr(st.login_url, bbep.width() / 2 + x, y);
  bbep.fullUpdate(CLEAR_FAST, false);

  Log_info("Apps: install screen shown, a long press stops it");
  iqs323_task_set_streaming_mode(true);
  iqs323_task_take_touch_starts();
  touch_reset();
  while (true) {
    delay(APP_POLL_MS * 5);
    if (pump_touch()) break;
    webshare_status(&st);
    if (!st.running) break; // idle for 15 minutes, or too many wrong codes
  }
  iqs323_task_set_streaming_mode(false);
  webshare_stop();
  show_last_image_and_sleep();
}

void apps_resume_if_due(void) {
  PreferencesPersistence persistence(preferences);
  char restored[32];
  app_install_recover(persistence, webshare_fs_root(), restored, sizeof(restored)); // a start that crashed
  if (restored[0]) Log_error("Apps: %s crashed while starting, the previous version was put back", restored);
  esp_reset_reason_t reason = esp_reset_reason();
  bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
                 reason == ESP_RST_WDT;
  char path[APPS_PATH_MAX];
  app_resume_t due = app_resume_take(persistence, crashed, path, sizeof(path));
  if (due == APP_RESUME_NONE) {
    if (crashed) Log_info("Apps: reset by a crash (%d), no app resumed", reason);
    return;
  }
  if (due == APP_RESUME_NO_INPUT) {
    char name[32];
    if (!app_path_name(path, name, sizeof(name))) snprintf(name, sizeof(name), "%s", path);
    Log_info("Apps: not resuming %s: no touch or key reached it before the restart", path);
    app_error_and_sleep(name, "not resumed: no touch or key reached it before the restart");
  }
  Log_info("Apps: resuming %s", path);
  apps_run(path);
}

#endif // BOARD_TRMNL_X
