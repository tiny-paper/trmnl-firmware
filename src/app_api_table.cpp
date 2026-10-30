// The calls the firmware hands a native app (app_api.h). Each checks its arguments: the app is
// not trusted, a bad pointer or path gets an error, not a crash where it can be avoided.
#ifdef BOARD_TRMNL_X

#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <app_clock.h>
#include <app_path.h>
#include <apps.h>
#include <battery.h>
#include <globals.h>
#include <power.h>
#include <epd_driver.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <stdarg.h>
#include <sys/time.h>
#include <trmnl_log.h>
#include <wifi_network.h>

#include "app_host.h"
#include "ble_keyboard.h"
#include "keyboards.h"
#include "usb_drive.h"
#include "webshare.h"

app_host_t app_host;

// ---- screen ----

static app_screen_mode_t screen_mode = APP_SCREEN_1BIT;

static int screen_width(void) { return bbep.width(); }
static int screen_height(void) { return bbep.height(); }

static int screen_stride(void) {
  return screen_mode == APP_SCREEN_1BIT ? (bbep.width() + 7) / 8 : (bbep.width() + 1) / 2;
}

// The panel driver is also the IO expander's (the USB port reads the charger through it)
struct panel_lock {
  panel_lock() { xSemaphoreTake(app_host.panel, portMAX_DELAY); }
  ~panel_lock() { xSemaphoreGive(app_host.panel); }
};

static uint8_t *screen_set_mode(app_screen_mode_t mode) {
  if (mode != APP_SCREEN_1BIT && mode != APP_SCREEN_GRAY4) return nullptr;
  panel_lock lock;
  if (bbep.setMode(mode == APP_SCREEN_1BIT ? BB_MODE_1BPP : BB_MODE_4BPP) != BBEP_SUCCESS) return nullptr;
  screen_mode = mode;
  bbep.fillScreen(mode == APP_SCREEN_1BIT ? BBEP_WHITE : 0xf);
  return bbep.currentBuffer();
}

static int screen_refresh(const app_rect_t *rect, app_refresh_t how) {
  BB_RECT r = {0, 0, bbep.width(), bbep.height()};
  if (rect) {
    if (rect->w <= 0 || rect->h <= 0 || rect->x < 0 || rect->y < 0 || rect->x + rect->w > bbep.width() ||
        rect->y + rect->h > bbep.height()) {
      return APP_EINVAL;
    }
    r = {rect->x, rect->y, rect->w, rect->h};
  }
  int rc;
  panel_lock lock;
  if (app_host.screen_held) return 0; // the pairing screen is up; it puts the app's pixels back when done
  if (how == APP_REFRESH_PARTIAL && screen_mode == APP_SCREEN_1BIT) {
    rc = bbep.partialUpdate(true, r.y, r.y + r.h - 1);
  } else {
    rc = bbep.fullUpdate(CLEAR_FAST, true, rect ? &r : nullptr);
  }
  return rc == BBEP_SUCCESS ? 0 : APP_EIO;
}

// ---- input, time, log ----

static bool input_poll(app_event_t *ev, uint32_t timeout_ms) {
  if (!ev) return false;
  return xQueueReceive(app_host.events, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static uint32_t app_millis(void) { return millis(); }

static int64_t wall_time(void) {
  time_t now = time(nullptr);
  return now > 1700000000 ? (int64_t)now : 0; // before late 2023: the clock was never set
}

static int battery_percent(bool *charging) {
  ChargingStatus status;
  {
    panel_lock lock; // the charger's STAT pin is on the expander the panel uses
    status = power().chargingStatus();
  }
  return app_battery_percent(battery().readSoc(), status, charging);
}

// The X's time zone: a POSIX TZ string in NVS (APP_TZ_KEY). TRMNL's firmware keeps none today, so this is UTC
// with zone_known 0 until something stores one
static int local_time(app_local_time_t *out) {
  String tz = preferences.getString(APP_TZ_KEY, "");
  return app_local_time(wall_time(), tz.c_str(), out);
}

static bool sleep_ms(uint32_t ms) {
  app_event_t ev;
  return xQueuePeek(app_host.events, &ev, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static void app_log(const char *fmt, ...) {
  if (!fmt) return;
  char line[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  Log_info("app %s: %s", app_host.name, line);
}

static void app_exit(int code) { app_host_finish(code); }

// ---- files ----

struct app_file {
  File file;
  bool used;
};

static app_file files[APP_HOST_MAX_FILES];

static bool full_path(const char *rel, char *out, size_t len) { return app_path_join(app_host.dir, rel, out, len); }

// The app's handle, if it is one of ours and open
static app_file *file_of(app_file_t *f) {
  for (auto &slot : files) {
    if (&slot == f && slot.used) return &slot;
  }
  return nullptr;
}

static app_file_t *file_open(const char *path, int mode) {
  char full[APP_PATH_MAX + 64];
  if (!full_path(path, full, sizeof(full)) || !path[0]) return nullptr;
  const char *m = mode == APP_FILE_READ ? FILE_READ : mode == APP_FILE_WRITE ? FILE_WRITE : FILE_APPEND;
  if (mode != APP_FILE_READ && mode != APP_FILE_WRITE && mode != APP_FILE_APPEND) return nullptr;
  if (mode != APP_FILE_READ && usb_drive_busy()) return nullptr; // the computer has the files
  if (mode == APP_FILE_READ && !LittleFS.exists(full)) return nullptr;
  for (auto &slot : files) {
    if (slot.used) continue;
    slot.file = LittleFS.open(full, m);
    if (!slot.file || slot.file.isDirectory()) {
      slot.file.close();
      return nullptr;
    }
    slot.used = true;
    return &slot;
  }
  return nullptr; // too many open
}

static int file_read(app_file_t *f, void *buf, size_t len) {
  app_file *slot = file_of(f);
  if (!slot || !buf) return APP_EINVAL;
  return (int)slot->file.read((uint8_t *)buf, len);
}

static int file_write(app_file_t *f, const void *buf, size_t len) {
  app_file *slot = file_of(f);
  if (!slot || !buf) return APP_EINVAL;
  size_t n = slot->file.write((const uint8_t *)buf, len);
  return n == len ? (int)n : APP_EIO; // short write: the file system is full
}

static int file_seek(app_file_t *f, uint32_t pos) {
  app_file *slot = file_of(f);
  if (!slot) return APP_EINVAL;
  return slot->file.seek(pos) ? 0 : APP_EINVAL;
}

static int file_close(app_file_t *f) {
  app_file *slot = file_of(f);
  if (!slot) return APP_EINVAL;
  slot->file.close();
  slot->used = false;
  return 0;
}

void app_host_close_files(void) {
  for (auto &slot : files) {
    if (slot.used) slot.file.close();
    slot.used = false;
  }
}

static int file_list(const char *dir, app_list_cb_t cb, void *ctx) {
  char full[APP_PATH_MAX + 64];
  if (!cb || !full_path(dir ? dir : "", full, sizeof(full))) return APP_EINVAL;
  File root = LittleFS.open(full);
  if (!root || !root.isDirectory()) return APP_ENOENT;
  int n = 0;
  while (File entry = root.openNextFile()) {
    bool go_on = cb(entry.name(), entry.isDirectory(), (uint32_t)entry.size(), ctx);
    entry.close();
    n++;
    if (!go_on) break;
  }
  root.close();
  return n;
}

static int file_rename(const char *from, const char *to) {
  char a[APP_PATH_MAX + 64], b[APP_PATH_MAX + 64];
  if (!full_path(from, a, sizeof(a)) || !full_path(to, b, sizeof(b)) || !from[0] || !to[0]) return APP_EINVAL;
  if (usb_drive_busy()) return APP_EBUSY;
  if (!LittleFS.exists(a)) return APP_ENOENT;
  return LittleFS.rename(a, b) ? 0 : APP_EIO;
}

static int file_remove(const char *path) {
  char full[APP_PATH_MAX + 64];
  if (!full_path(path, full, sizeof(full)) || !path[0]) return APP_EINVAL;
  if (usb_drive_busy()) return APP_EBUSY;
  if (!LittleFS.exists(full)) return APP_ENOENT;
  File f = LittleFS.open(full);
  bool dir = f && f.isDirectory();
  f.close();
  return (dir ? LittleFS.rmdir(full) : LittleFS.remove(full)) ? 0 : APP_EIO;
}

static int file_mkdir(const char *path) {
  char full[APP_PATH_MAX + 64];
  if (!full_path(path, full, sizeof(full)) || !path[0]) return APP_EINVAL;
  if (usb_drive_busy()) return APP_EBUSY;
  return LittleFS.mkdir(full) ? 0 : APP_EIO;
}

// ---- net ----

#define APP_WIFI_TIMEOUT_MS 15000

static bool net_up(void) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!WifiCaptivePortal.isSaved()) return false;
  // 5 GHz networks go through the modem, whose AT interface has no HTTP client for apps yet
  if (WifiCaptivePortal.getLastCredentials().is5GHz) return false;
  WiFi.mode(WIFI_STA);
  bool up = connectWithSavedCredentials();
  // Sharing the radio with a Bluetooth keyboard: modem sleep lets the coexistence scheduler give BLE its slots
  // (TRMNL's connect turns sleep off)
  if (up && ble_keyboard_radio_on()) WiFi.setSleep(WIFI_PS_MIN_MODEM);
  return up;
}

bool app_host_net_up(void) { return net_up(); }

static int http_request(const app_http_request_t *req, void *buf, size_t buf_len, app_http_response_t *resp) {
  if (!req || !req->url || !resp || (!buf && buf_len)) return APP_EINVAL;
  *resp = {APP_ENONET, 0, false};
  if (!net_up()) return APP_ENONET;
  esp_http_client_config_t config = {};
  config.url = req->url;
  config.timeout_ms = req->timeout_ms ? (int)req->timeout_ms : 15000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return APP_ENOMEM;
  const char *method = req->method ? req->method : "GET";
  esp_http_client_method_t m = strcmp(method, "POST") == 0     ? HTTP_METHOD_POST
                               : strcmp(method, "PUT") == 0    ? HTTP_METHOD_PUT
                               : strcmp(method, "DELETE") == 0 ? HTTP_METHOD_DELETE
                               : strcmp(method, "PATCH") == 0  ? HTTP_METHOD_PATCH
                               : strcmp(method, "HEAD") == 0   ? HTTP_METHOD_HEAD
                                                               : HTTP_METHOD_GET;
  esp_http_client_set_method(client, m);
  for (const char *const *h = req->headers; h && *h; h++) {
    const char *colon = strchr(*h, ':');
    if (!colon || colon - *h >= 64) continue;
    char name[64];
    memcpy(name, *h, colon - *h);
    name[colon - *h] = '\0';
    const char *value = colon + 1;
    while (*value == ' ') value++;
    esp_http_client_set_header(client, name, value);
  }
  int rc = APP_ETIMEDOUT;
  if (esp_http_client_open(client, req->body ? (int)req->body_len : 0) == ESP_OK) {
    if (!req->body || esp_http_client_write(client, (const char *)req->body, (int)req->body_len) == (int)req->body_len) {
      esp_http_client_fetch_headers(client);
      resp->status = esp_http_client_get_status_code(client);
      size_t got = 0;
      while (got < buf_len) {
        int n = esp_http_client_read(client, (char *)buf + got, (int)(buf_len - got));
        if (n <= 0) break;
        got += (size_t)n;
      }
      char probe;
      resp->truncated = got == buf_len && esp_http_client_read(client, &probe, 1) > 0;
      resp->body_len = got;
      rc = 0;
    }
  }
  if (rc != 0) resp->status = rc;
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return rc;
}

// ---- settings ----

#define APP_SETTING_MAX 256

// The app's NVS namespace: "app" and a hash of its name (namespaces are at most 15 characters)
static void settings_namespace(char *out, size_t len) {
  uint32_t h = 2166136261u;
  for (const char *c = app_host.name; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  snprintf(out, len, "app%08lx", (unsigned long)h);
}

static bool key_ok(const char *key) { return key && key[0] && strlen(key) <= 15; }

static int setting_get(const char *key, void *buf, size_t len) {
  if (!key_ok(key) || (!buf && len)) return APP_EINVAL;
  char ns[16];
  settings_namespace(ns, sizeof(ns));
  Preferences p;
  if (!p.begin(ns, true)) return APP_ENOENT; // nothing saved yet
  int rc = APP_ENOENT;
  size_t stored = p.isKey(key) ? p.getBytesLength(key) : 0;
  if (stored > 0) rc = stored > len ? APP_EINVAL : (int)p.getBytes(key, buf, len);
  p.end();
  return rc;
}

static int setting_set(const char *key, const void *buf, size_t len) {
  if (!key_ok(key) || !buf || len == 0 || len > APP_SETTING_MAX) return APP_EINVAL;
  char ns[16];
  settings_namespace(ns, sizeof(ns));
  Preferences p;
  if (!p.begin(ns, false)) return APP_EIO;
  int rc = p.putBytes(key, buf, len) == len ? 0 : APP_EIO;
  p.end();
  return rc;
}

static const app_api_t table = {
    APP_API_VERSION, sizeof(app_api_t),
    screen_width,    screen_height,
    screen_set_mode, screen_stride,
    screen_refresh,  input_poll,
    file_open,       file_read,
    file_write,      file_seek,
    file_close,      file_list,
    file_rename,     file_remove,
    file_mkdir,      app_millis,
    wall_time,       sleep_ms,
    app_log,         net_up,
    http_request,    setting_get,
    setting_set,     app_exit,
    keyboards_enable, keyboards_status,
    usb_drive_start, usb_drive_state,
    usb_drive_stop,
    webshare_start,  webshare_stop,
    webshare_status,
    battery_percent, local_time,
};

const app_api_t *app_api_table(void) {
  screen_mode = APP_SCREEN_1BIT;
  return &table;
}

#endif // BOARD_TRMNL_X
