#ifndef APP_API_H
#define APP_API_H

// The interface between the firmware and a native app: an ELF file in /apps on the flash file system
// (docs/apps.md). The firmware loads the app, then calls its entry with a table of function pointers:
//
//   int app_main(const app_api_t *api);
//
// The app returns 0 when done, anything else for an error the firmware shows. The table only grows:
// new calls are added at the end and raise APP_API_VERSION, so an app checks `api->size` before
// using a call newer than the version it was built for. The firmware refuses an app whose
// app_info_t asks for a newer version than it has.
//
// Plain C, no Arduino or ESP-IDF types: apps are built without the firmware's headers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_API_VERSION 1

// ---- app header ----

// Every app holds one app_info_t named `app_info`, in the ELF section APP_INFO_SECTION, which the
// firmware reads before loading anything. APP_INFO() declares it; the link keeps it with
// -Wl,--undefined=app_info (apps/build.sh).
#define APP_INFO_SECTION ".trmnl_app_info"
#define APP_INFO_MAGIC 0x50504154u // "TAPP" in little-endian

typedef struct {
  uint32_t magic; // APP_INFO_MAGIC
  uint32_t api_version; // the APP_API_VERSION the app was built against
  uint32_t stack_size; // bytes for the app's task (internal RAM); 0 for the default (APP_DEFAULT_STACK)
  char name[32]; // shown in the Apps menu, NUL-terminated
} app_info_t;

#define APP_DEFAULT_STACK (16 * 1024)
#define APP_MAX_STACK (64 * 1024)

#ifdef __cplusplus
#define APP_INFO_LINKAGE extern "C"
#else
#define APP_INFO_LINKAGE
#endif
#define APP_INFO(app_name, stack)                                                                                      \
  APP_INFO_LINKAGE __attribute__((used, section(APP_INFO_SECTION))) const app_info_t app_info = {                      \
      APP_INFO_MAGIC, APP_API_VERSION, (stack), app_name}

// ---- screen ----

typedef enum {
  APP_SCREEN_1BIT = 1, // 1 bit per pixel, 8 pixels per byte, MSB leftmost, 1 = white
  APP_SCREEN_GRAY4 = 4, // 4 bits per pixel, 2 pixels per byte, high nibble leftmost, 0 = black, 15 = white
} app_screen_mode_t;

typedef enum {
  APP_REFRESH_PARTIAL, // fast, no flashing; may leave ghosts. 1-bit mode only, a gray screen gets a full one
  APP_REFRESH_FULL, // clears ghosting, flashes
} app_refresh_t;

typedef struct {
  int16_t x, y, w, h;
} app_rect_t;

// ---- input ----

typedef enum {
  APP_EVENT_NONE,
  APP_EVENT_KEY_DOWN, // a key (USB or serial keyboard)
  APP_EVENT_KEY_UP,
  APP_EVENT_TOUCH, // a touch bar gesture
  APP_EVENT_EXIT, // the user asked to leave (long press): save and return within APP_EXIT_GRACE_MS
} app_event_type_t;

#define APP_EXIT_GRACE_MS 2000

// Key codes: printable keys carry their character in `utf8`; others have one of these
typedef enum {
  APP_KEY_NONE = 0,
  APP_KEY_ENTER = 0x0d,
  APP_KEY_ESCAPE = 0x1b,
  APP_KEY_BACKSPACE = 0x08,
  APP_KEY_TAB = 0x09,
  APP_KEY_DELETE = 0x7f,
  APP_KEY_UP = 0x100,
  APP_KEY_DOWN,
  APP_KEY_LEFT,
  APP_KEY_RIGHT,
  APP_KEY_HOME,
  APP_KEY_END,
  APP_KEY_PAGE_UP,
  APP_KEY_PAGE_DOWN,
  APP_KEY_F1, // F2..F12 follow
  APP_KEY_CHAR = 0x200, // printable: see utf8
} app_key_t;

#define APP_MOD_SHIFT 0x01
#define APP_MOD_CTRL 0x02
#define APP_MOD_ALT 0x04
#define APP_MOD_META 0x08

typedef enum {
  APP_TOUCH_LEFT,
  APP_TOUCH_MIDDLE,
  APP_TOUCH_RIGHT,
} app_touch_zone_t;

typedef enum {
  APP_GESTURE_TAP,
  APP_GESTURE_SWIPE_LEFT,
  APP_GESTURE_SWIPE_RIGHT,
} app_gesture_t;

typedef struct {
  uint8_t type; // app_event_type_t
  uint8_t modifiers; // APP_MOD_*
  uint16_t key; // app_key_t, for KEY_DOWN / KEY_UP
  char utf8[5]; // the character typed, NUL-terminated; empty for non-printable keys
  uint8_t zone; // app_touch_zone_t, for TOUCH
  uint8_t gesture; // app_gesture_t, for TOUCH
} app_event_t;

// ---- files ----

typedef struct app_file app_file_t; // opaque

// open() modes
#define APP_FILE_READ 0
#define APP_FILE_WRITE 1 // create or truncate
#define APP_FILE_APPEND 2

// list() callback: return false to stop
typedef bool (*app_list_cb_t)(const char *name, bool is_dir, uint32_t size, void *ctx);

// ---- net ----

typedef struct {
  const char *method; // "GET", "POST", ...
  const char *url; // https:// or http://
  const char *const *headers; // "Name: value" strings, nullptr-terminated; may be nullptr
  const void *body; // may be nullptr
  size_t body_len;
  uint32_t timeout_ms; // 0 for 15 s
} app_http_request_t;

typedef struct {
  int status; // HTTP status, or < 0 for a transport error (no Wi-Fi, DNS, TLS, timeout)
  size_t body_len; // bytes written to the buffer
  bool truncated; // the body did not fit
} app_http_response_t;

// ---- the table ----

// Calls returning int: 0 or a count on success, < 0 (an APP_E* value) on failure. Paths are relative
// to the app's own folder (/appdata/<app>/); "..", absolute paths and names longer than
// APP_PATH_MAX are refused.
#define APP_PATH_MAX 64

#define APP_EINVAL -1 // bad argument or path
#define APP_ENOENT -2 // no such file or key
#define APP_ENOMEM -3
#define APP_EIO -4 // read or write failed, disk full
#define APP_ENOTSUP -5
#define APP_ETIMEDOUT -6
#define APP_ENONET -7 // no Wi-Fi

typedef struct app_api {
  uint32_t version; // APP_API_VERSION of the firmware
  uint32_t size; // sizeof(app_api_t) in the firmware

  // Screen. The framebuffer is the firmware's, valid until exit; draw into it, then refresh.
  int (*screen_width)(void);
  int (*screen_height)(void);
  // Switches the mode (clearing to white) and returns the framebuffer, or nullptr
  uint8_t *(*screen_set_mode)(app_screen_mode_t mode);
  int (*screen_stride)(void); // bytes per row in the current mode
  // Pushes a rectangle (nullptr: the whole screen) to the panel; blocks until done
  int (*screen_refresh)(const app_rect_t *rect, app_refresh_t how);

  // Input. Waits up to timeout_ms (0: do not wait) for an event; returns true with *ev filled
  bool (*input_poll)(app_event_t *ev, uint32_t timeout_ms);

  // Files, under the app's folder
  app_file_t *(*file_open)(const char *path, int mode);
  int (*file_read)(app_file_t *f, void *buf, size_t len);
  int (*file_write)(app_file_t *f, const void *buf, size_t len);
  int (*file_seek)(app_file_t *f, uint32_t pos);
  int (*file_close)(app_file_t *f);
  int (*file_list)(const char *dir, app_list_cb_t cb, void *ctx);
  int (*file_rename)(const char *from, const char *to);
  int (*file_remove)(const char *path);
  int (*file_mkdir)(const char *path);

  // Time
  uint32_t (*millis)(void); // since boot
  int64_t (*wall_time)(void); // Unix seconds, or 0 when the clock is not set
  // Sleep: yields to other tasks. Returns early (true) when an event is waiting
  bool (*sleep_ms)(uint32_t ms);

  // Log, printf-style, to the serial console and the device log
  void (*log)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

  // Net: connects Wi-Fi if needed (saved credentials). Blocks; the body goes into buf
  bool (*net_up)(void);
  int (*http_request)(const app_http_request_t *req, void *buf, size_t buf_len, app_http_response_t *resp);

  // Settings: small values (up to 256 bytes) per app, kept in NVS. get returns the length
  int (*setting_get)(const char *key, void *buf, size_t len);
  int (*setting_set)(const char *key, const void *buf, size_t len);

  // Ends the app now (does not return), as if app_main returned `code`
  void (*exit)(int code);
} app_api_t;

#ifdef __cplusplus
}
#endif

#endif // APP_API_H
