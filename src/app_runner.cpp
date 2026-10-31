// Loads and runs a native app (apps.h, docs/apps.md) with Espressif's elf_loader, and supervises it:
// feeds it touch bar and serial key events, ends it on a long press, and goes back to TRMNL.
#ifdef BOARD_TRMNL_X

#include <LittleFS.h>
#include "app_heap.h"
#include <app_elf_check.h>
#include <app_install.h>
#include <app_keys.h>
#include <app_path.h>
#include <app_resume.h>
#include <app_serial.h>
#include <app_sleep_gate.h>
#include <apps.h>
#include <bl.h>
#include <cached_images.h>
#include <epd_driver.h>
#include <esp_elf.h>
#include <esp_heap_caps.h>
#include <filesystem.h>
#include <fonts.h>
#include <hints.h>
#include <globals.h>
#include <preferences_persistence.h>
#include <qrcodegen.h>
#include <touchbar_input.h>
#include <touchbar_layout.h>
#include <trmnl_log.h>

#include <algorithm>

#include "app_host.h"
#include "ble_keyboard.h"
#include "evlog.h"
#include "espnow_app.h"
#include "evlog_crash.h"
#include "keyboards.h"
#include "orientation_x.h"
#include "usb_drive.h"
#include "usb_port_role.h"
#include "webshare.h"

#define APP_POLL_MS       20
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
  evlog_add("Apps: %s: %s", name, what);
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

static app_key_decoder_t keys;
static uint32_t last_key_byte;

static void post(const app_event_t &ev) {
  if (!app_host.events) return; // a firmware screen (Install via Wi-Fi): no app to take it
  if (ev.type != APP_EVENT_ORIENTATION) app_host.got_input = true; // turning the X proves no working touch bar
  if (xQueueSend(app_host.events, &ev, 0) != pdTRUE) Log_info("Apps: event queue full, event dropped");
}

static void post_tap(int channel, uint8_t gesture) {
  if (ble_keyboard_tap(channel)) return; // the Bluetooth pairing screen's
  app_event_t ev = {};
  ev.type = APP_EVENT_TOUCH;
  // in the user's frame once the app follows the X's position (app_api.h orientation_follow)
  ev.zone = (uint8_t)(orientation_x_following() ? touchbar_zone_of_channel(orientation_x_steady(), channel) : channel);
  ev.gesture = gesture;
  post(ev);
}

// Reads the touch bar (touchbar_input.h, the menu's path): taps to the app or the pairing screen; true on a long press
static bool pump_touch(void) {
  touch_event_t evs[TOUCHBAR_INPUT_MAX];
  int n = touchbar_input_poll(evs);
  bool long_press = false;
  for (int i = 0; i < n; i++) {
    if (evs[i].gesture == touch_gesture_t::TAP) post_tap(evs[i].channel, APP_GESTURE_TAP);
    if (evs[i].gesture == touch_gesture_t::HOLD) post_tap(evs[i].channel, APP_GESTURE_HOLD); // only after touch_hold
    if (evs[i].gesture == touch_gesture_t::LONG) long_press = true;
  }
  return long_press;
}

// A finished serial line that is not keys (app_serial.h): "TAPP NORESUME" (way out without the touch bar, docs/apps.md:
// nothing is resumed after the restart that follows) and the event log's "TAPP EVLOG ..."; others are ignored
#define APP_CMD_NORESUME "TAPP NORESUME"
static void serial_command(const char *line) {
  const char *ev = strstr(line, "TAPP EVLOG"); // noise before it on the line ignored
  if (ev && evlog_serial_command(ev)) return;
  size_t n = strlen(line), c_len = sizeof(APP_CMD_NORESUME) - 1;
  if (n < c_len || strcmp(line + n - c_len, APP_CMD_NORESUME) != 0) return;
  PreferencesPersistence persistence(preferences);
  app_resume_ended(persistence);
  Log_info("Apps: resume cleared by serial command - restarting into TRMNL");
  evlog_add("Apps: resume cleared by serial command, restarting");
  evlog_flush(false);
  delay(100);
  ESP.restart();
}

static app_serial_t serial_in;

// Reads the serial port: only "TKEY <text>" lines type into the app (a terminal or the sim's tests); TAPP commands and
// every other byte never reach it as keys (device, b0ff008: "TAPP EVLOG" was typed into the writer's document)
static void pump_keys(void) {
  app_event_t ev;
  while (Serial.available() > 0) {
    int b = Serial.read();
    if (b < 0) break;
    uint8_t key;
    const char *line;
    app_serial_out_t o = app_serial_feed(&serial_in, (uint8_t)b, &key, &line);
    if (o == APP_SERIAL_LINE) serial_command(line);
    if (o != APP_SERIAL_KEY) continue;
    last_key_byte = millis();
    if (!app_key_feed(&keys, key, &ev)) continue;
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
  app_heap_route();
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

static bool app_exit_asked; // a long press asked the running app to exit

// Waits for the app to end, pumping its input. Returns false if it was told to exit and did not
// within APP_EXIT_GRACE_MS.
static bool app_supervise(PreferencesPersistence &persistence) {
  uint32_t started = millis();
  app_exit_asked = false;
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
    int turned = orientation_x_poll(millis());
    if (turned >= 0 && orientation_x_following()) {
      app_event_t ev = {};
      ev.type = APP_EVENT_ORIENTATION;
      ev.key = (uint16_t)turned;
      post(ev);
    }
    if (!usb_drive_busy()) pump_keys(); // the serial port is off while the drive shows
    if (pump_touch() && !exit_asked) {
      Log_event("Apps: long press - asking %s to exit", app_host.name);
      app_resume_ended(persistence);
      app_event_t ev = {};
      ev.type = APP_EVENT_EXIT;
      xQueueReset(app_host.events);
      post(ev);
      exit_asked = millis();
      app_exit_asked = true;
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
  Log_event("Apps: starting %s", path);
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
  app_serial_init(&serial_in);
  Serial.begin(115200); // the keys of a terminal on the USB serial port
  touchbar_input_begin(app_host.name); // the tap that started it is not the app's
  orientation_x_begin();

  TaskHandle_t task = nullptr;
  if (!app_host.events || !app_host.done || !app_host.panel ||
      xTaskCreatePinnedToCore(app_task, "app", info.stack_size, nullptr, 1, &task, 1) != pdPASS) {
    esp_elf_deinit(&elf);
    app_resume_ended(persistence);
    app_error_and_sleep(app_host.name, with_restore_note(persistence, "not enough memory for its stack"));
  }
  app_sleep_gate_app_started(app_host.name);
  Log_info("Apps: %s (%s) running, %lu bytes of stack", info.name, path, (unsigned long)info.stack_size);
  // For tools/app_addr2line.py: the app's frames in a panic backtrace are entry - e_entry + their ELF address
  Log_info("Apps: %s entry at 0x%08lx", app_host.name, (unsigned long)(uintptr_t)elf.entry);
  evlog_crash_app(app_host.name, (uint32_t)(uintptr_t)elf.entry); // a crash's PC as an offset into the app

  if (!app_supervise(persistence)) {
    // Its task may hold a lock or be anywhere in its code: only a restart cleans that up
    Log_error("Apps: %s did not exit within %d ms - restarting into TRMNL", app_host.name, APP_EXIT_GRACE_MS);
    app_sleep_gate_app_ended();
    app_resume_ended(persistence);
    evlog_crash_app(nullptr, 0);
    evlog_add("Apps: %s did not exit within %d ms, restarting", app_host.name, APP_EXIT_GRACE_MS);
    evlog_flush(false);
    delay(100);
    ESP.restart();
  }
  app_sleep_gate_app_ended(); // the device is TRMNL's again: its error screen or image, then sleep
  usb_drive_end_for_exit(); // serial back, the computer's changes taken or not, before anything else
  int code = app_host.code;
  evlog_crash_app(nullptr, 0);
  // why it ended: the user's long press, the app quitting by itself (return or exit), or its error
  const char *how = app_exit_asked ? "after a long press" : code == 0 ? "quit by itself" : "error";
  Log_event("Apps: %s ended with %d (%s)", app_host.name, code, how);
  keyboards_end(); // the USB port back in device mode before anything else
  usb_port_give(usb_port_role()); // whatever still holds the PHY: serial again (usb_port_role.h)
  touchbar_input_end();
  orientation_x_end();
  webshare_stop(); // the page never outlives the app
  espnow_app_stop(); // nor ESP-NOW: the original MAC back, the radio off (a crash restarts with the eFuse MAC)
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

// The fonts do not scale, so a string is drawn small in the top left corner (the scratch area), measured by its
// black pixels, and blown up pixel by pixel to (x, y). The scratch area is wiped again. The screen keeps rotation 0
// (nothing here sets another), so the 1 bit buffer is read as bbSetPixel2Clr writes it: MSB first, white = 1.
#define SCRATCH_W 1000
#define SCRATCH_H 64
static bool scratch_black(int x, int y) {
  const int pitch = (bbep.width() + 7) >> 3;
  return !(bbep.currentBuffer()[(x >> 3) + y * pitch] & (0x80 >> (x & 7)));
}

// Draws `text` `scale` times larger, its left edge at x (or centered on x when `center`), its top at y; returns the
// height used. A scale that would run past the panel is lowered.
static int draw_big(const char *text, int x, int y, int scale, bool center, const void *font) {
  bbep.setFont(font);
  bbep.fillRect(0, 0, SCRATCH_W, SCRATCH_H, BBEP_WHITE);
  bbep.drawString(text, 0, SCRATCH_H - 16);
  int x0 = SCRATCH_W, x1 = -1, y0 = SCRATCH_H, y1 = -1;
  for (int row = 0; row < SCRATCH_H; row++) {
    for (int col = 0; col < SCRATCH_W; col++) {
      if (!scratch_black(col, row)) continue;
      if (col < x0) x0 = col;
      if (col > x1) x1 = col;
      if (row < y0) y0 = row;
      if (row > y1) y1 = row;
    }
  }
  if (x1 < 0) return 0;
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  const int margin = bbep.width() / 24;
  while (scale > 1 && w * scale > bbep.width() - 2 * margin) scale--;
  if (center) x -= w * scale / 2;
  if (x < margin) x = margin;
  // copy the glyph pixels to a list of runs first: the destination may overlap the scratch area
  for (int row = h - 1; row >= 0; row--) {
    int col = 0;
    while (col < w) {
      if (!scratch_black(x0 + col, y0 + row)) { col++; continue; }
      int run = col;
      while (col < w && scratch_black(x0 + col, y0 + row)) col++;
      bbep.fillRect(x + run * scale, y + row * scale, (col - run) * scale, scale, BBEP_BLACK);
    }
  }
  bbep.fillRect(0, 0, SCRATCH_W, SCRATCH_H, BBEP_WHITE);
  return h * scale;
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
  const int W = bbep.width(), margin = W / 24, mid = W / 2;
  bbep.setMode(BB_MODE_1BPP);
  bbep.fillScreen(BBEP_WHITE);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  // Everything below is drawn from y = 70 on (below the scratch area, whose rows are still read while drawing): the scratch area of draw_big is the top left corner
  draw_big("Install apps via Wi-Fi", mid, 70, 2, true, Roboto_Black_24);
  char line[96];
  int y = 180;
  if (st.mode != APP_WEBSHARE_HOME) {
    snprintf(line, sizeof(line), "Join the network \"%s\", then open", st.network);
    y += draw_big(line, mid, y, 2, true, Inter_18) + 24;
  } else {
    y += draw_big("Open on the same Wi-Fi", mid, y, 2, true, Inter_18) + 24;
  }
  y += draw_big(st.url, mid, y, 3, true, Roboto_Black_24) + 40;
  y += draw_big("Code", mid, y, 2, true, Inter_18) + 25;
  y += draw_big(st.code, mid, y, 8, true, Roboto_Black_24) + 50;
  // the QR at least as large as before (12 px modules), left; the hints right of it
  int qr_top = y;
  draw_qr(st.login_url, margin, qr_top);
  int tx = margin + 700;
  int ty = qr_top + 60;
  ty += draw_big("Scan the code", tx, ty, 2, false, Inter_18) + 20;
  ty += draw_big("or type the number", tx, ty, 2, false, Inter_18) + 20;
  draw_big("on the page.", tx, ty, 2, false, Inter_18);
  hints_draw_long_press("stop", bbep.height() - HINTS_MARGIN);
  bbep.fullUpdate(CLEAR_FAST, false);

  app_sleep_gate_screen("Install via Wi-Fi");
  Log_info("Apps: install screen shown, a long press stops it");
  touchbar_input_begin("Install via Wi-Fi");
  while (true) {
    delay(APP_POLL_MS * 5);
    if (pump_touch()) break;
    webshare_status(&st);
    if (!st.running) { // idle for 15 minutes, or too many wrong codes
      evlog_add("Apps: install page stopped by itself (idle or locked out)");
      break;
    }
  }
  touchbar_input_end();
  app_sleep_gate_screen(nullptr);
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
    if (crashed) Log_event("Apps: reset by a crash (%d), no app resumed", reason);
    return;
  }
  if (due == APP_RESUME_NO_INPUT) {
    char name[32];
    if (!app_path_name(path, name, sizeof(name))) snprintf(name, sizeof(name), "%s", path);
    Log_info("Apps: not resuming %s: no touch or key reached it before the restart", path);
    app_error_and_sleep(name, "not resumed: no touch or key reached it before the restart");
  }
  Log_event("Apps: resuming %s", path);
  apps_run(path);
}

#endif // BOARD_TRMNL_X
