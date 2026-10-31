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

#define APP_API_VERSION 2 // 2: keyboards, USB drive, Wi-Fi page (docs/apps.md "API 2")
// Calls added to a version without a new one: an app checks api->size >= APP_API_SLOT(n + 1) before call n
#define APP_API_MINOR 6 // 2.1: event_log_lines; 2.2: screen_orientation; 2.3: webshare_start_files;
                        // 2.4: ESP-NOW receive, touch hold; 2.5: orientation from the accelerometer;
                        // 2.6: keyboard layout, settings form

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
// app_rodata_align makes .rodata 4-byte aligned: elf_loader copies .text rounded up to 4 bytes and
// would take a .rodata starting in that rounding for code (kept by -Wl,--undefined=app_rodata_align)
#define APP_INFO(app_name, stack)                                                                                      \
  APP_INFO_LINKAGE __attribute__((used, aligned(4))) const uint32_t app_rodata_align = 0;                              \
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

// screen_orientation (API 2.2): how TRMNL shows its screens, so an app can match it. The framebuffer stays landscape
// (screen_width > screen_height); a portrait app draws turned: its (x, y) is the framebuffer's (y, height - 1 - x),
// as TRMNL turns a portrait image
#define APP_ORIENT_LANDSCAPE 0
#define APP_ORIENT_PORTRAIT 1

// API 2.5: the X's four positions from its accelerometer, named by where the touch bar is for the user. 0 and 1 are the
// two values above. An app that draws in position o maps its (x, y) to the framebuffer with app_orient_to_fb below;
// its screen is W x H (W, H: screen_width, screen_height) in 0 and 2, H x W in 1 and 3
#define APP_ORIENT_BAR_BOTTOM 0 // landscape, the panel's own frame
#define APP_ORIENT_BAR_LEFT 1 // portrait, turned as TRMNL turns a portrait image (APP_ORIENT_PORTRAIT)
#define APP_ORIENT_BAR_TOP 2 // landscape upside down
#define APP_ORIENT_BAR_RIGHT 3 // portrait the other way

// An app point (x, y) in position `o` to the framebuffer's (*fx, *fy); W, H: screen_width(), screen_height()
static inline void app_orient_to_fb(int o, int W, int H, int x, int y, int *fx, int *fy) {
  switch (o) {
  case APP_ORIENT_BAR_LEFT: *fx = y; *fy = H - 1 - x; break;
  case APP_ORIENT_BAR_TOP: *fx = W - 1 - x; *fy = H - 1 - y; break;
  case APP_ORIENT_BAR_RIGHT: *fx = W - 1 - y; *fy = x; break;
  default: *fx = x; *fy = y; break;
  }
}

// Where the touch bar is on an app's screen in one position (touchbar_layout, API 2.5), in that position's app
// coordinates: the edge it lies along and its three zones as strips of the screen along that edge, APP_TOUCHBAR_DEPTH
// deep. zone[APP_TOUCH_LEFT] is the zone the user has on the left (on the top in a portrait position), the same zone
// orientation_follow's touch events name; draw a zone's hint inside or next to its strip
#define APP_EDGE_BOTTOM 0
#define APP_EDGE_LEFT 1
#define APP_EDGE_TOP 2
#define APP_EDGE_RIGHT 3
#define APP_TOUCHBAR_DEPTH 160

typedef struct {
  int16_t width, height; // the app's screen in this position
  uint8_t orientation; // APP_ORIENT_*
  uint8_t edge; // APP_EDGE_*
  uint16_t reserved;
  app_rect_t zone[3]; // by app_touch_zone_t
} app_touchbar_t;

// ---- input ----

typedef enum {
  APP_EVENT_NONE,
  APP_EVENT_KEY_DOWN, // a key (USB or serial keyboard)
  APP_EVENT_KEY_UP,
  APP_EVENT_TOUCH, // a touch bar gesture
  APP_EVENT_EXIT, // the user asked to leave (long press): save and return within APP_EXIT_GRACE_MS
  APP_EVENT_KEYBOARD, // a keyboard came or went: `key` holds the APP_KBD_* bits connected now (API 2)
  APP_EVENT_ORIENTATION, // API 2.5, only after orientation_follow(true, ..): the X was turned, `key` is the new APP_ORIENT_*
  APP_EVENT_SETTINGS, // API 2.6: the phone saved the settings form; `key` is how many values changed (read them again)
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
  APP_KEY_HID = 0x300, // a key of a USB or Bluetooth keyboard whose character depends on the layout: see hid_usage
} app_key_t;

#define APP_MOD_SHIFT 0x01
#define APP_MOD_CTRL 0x02
#define APP_MOD_ALT 0x04
#define APP_MOD_META 0x08
#define APP_MOD_ALTGR 0x10 // right Alt of a USB or Bluetooth keyboard (not APP_MOD_ALT): the layout's third level

typedef enum {
  APP_TOUCH_LEFT,
  APP_TOUCH_MIDDLE,
  APP_TOUCH_RIGHT,
} app_touch_zone_t;

typedef enum {
  APP_GESTURE_TAP,
  APP_GESTURE_SWIPE_LEFT,
  APP_GESTURE_SWIPE_RIGHT,
  APP_GESTURE_HOLD, // API 2.4, only after touch_hold(): the zone held APP_TOUCH_HOLD_MS, released before the long press
} app_gesture_t;

#define APP_TOUCH_HOLD_MS 600 // a hold; from 1500 ms (the long press) the app is asked to exit as before

typedef struct {
  uint8_t type; // app_event_type_t
  uint8_t modifiers; // APP_MOD_*
  uint16_t key; // app_key_t, for KEY_DOWN / KEY_UP
  char utf8[5]; // the character typed, NUL-terminated; empty for non-printable keys
  uint8_t zone; // app_touch_zone_t, for TOUCH
  uint8_t gesture; // app_gesture_t, for TOUCH
  // For keys of a USB or Bluetooth keyboard: the HID usage (keyboard page, 0x04 = the key labelled A on a US
  // layout), 0 otherwise. Enter, Escape, Backspace, Tab, Delete, the arrows, Home, End, Page Up/Down and F1..F12 come
  // with their app_key_t. Since 2.6 a key that types a character on the X's keyboard layout (chosen on the Wi-Fi page)
  // comes as APP_KEY_CHAR with utf8 (Ctrl, Alt, Meta in `modifiers`, the character unchanged by them); every other key
  // as APP_KEY_HID. hid_usage is always there for apps that want key positions (API 2)
  uint8_t hid_usage;
} app_event_t;

#define APP_KBD_USB 0x01 // a USB keyboard on the X's port (needs an OTG adapter; the X powers it)
#define APP_KBD_BLE 0x02 // a Bluetooth LE keyboard

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
  int status; // HTTP status, or < 0: APP_ENONET (no Wi-Fi), APP_ENOMEM (no RAM for TLS), APP_ETIMEDOUT (DNS, TLS)
  size_t body_len; // bytes written to the buffer
  bool truncated; // the body did not fit
} app_http_response_t;

// ---- USB drive (version 2, docs/apps.md "USB drive") ----

// The app's writer layout (documents/<folder>/<title>.md|.txt, settings/settings.json,
// settings/fonts/) shown to a computer as a USB drive. While it shows, serial over the cable is
// off and the app's file writes return APP_EBUSY.
typedef enum {
  APP_DRIVE_OFF,
  APP_DRIVE_SHOWN, // waiting for a computer, or in use by one
  APP_DRIVE_IN_USE, // a computer has it
} app_drive_state_t;

typedef enum {
  APP_DRIVE_END_NONE,
  APP_DRIVE_END_EJECTED, // the computer ejected it
  APP_DRIVE_END_UNPLUGGED, // cable pulled (or the computer slept) without an eject
  APP_DRIVE_END_TIMEOUT, // no computer within connect_timeout_ms
  APP_DRIVE_END_STOPPED, // usb_drive_stop, or the app ended
} app_drive_end_t;

typedef struct {
  int32_t result; // 0, or an APP_E* value: nothing from the computer was taken
  uint8_t ended_by; // app_drive_end_t
  uint8_t taken; // the computer's changes are on the X
  uint8_t cut; // the computer was still writing when it went: nothing taken (copy again)
  uint8_t reserved;
  uint16_t shown; // files on the drive
  uint16_t not_shown; // layout files the drive could not hold (names differing only in case)
  uint16_t added, updated;
  uint16_t conflicts; // changed on both sides: "<title> (computer).md" kept beside, listed in .drive/conflicts.txt
  uint16_t kept_deleted; // deleted on the computer, kept on the X
  uint16_t bad_names; // not taken: name too long or not a layout name
  uint16_t left_alone; // other files on the drive
} app_drive_result_t;

// ---- Wi-Fi page (API 2) ----

// A page on the X's own Wi-Fi network (with a captive portal) or on the home network at <host>.local, where a phone
// browser lists, downloads (one file or a zip), uploads, renames and deletes the app's `documents/` files and adds
// fonts to `settings/fonts/`. Network name, host name, limits and timeouts are the firmware's config/apps.json
// (by default "Writer", writer.local, 15 min); the names below are those defaults. Nothing is ever overwritten: an
// upload that meets a different file of the same name is kept next to it as "name (web N).ext". The page is off
// until the app starts it, and off again when the app ends or after the idle time (status.idle_left_s). A 6-digit
// code, shown by the app, must be typed on the page first (docs/apps.md).
typedef enum {
  APP_WEBSHARE_AUTO = 0, // the default: the saved home network, so the phone stays on its Wi-Fi; only when none is
                         // reachable the access point below (webshare_status says which)
  APP_WEBSHARE_AP = 1, // own access point "Writer", open, address http://4.3.2.1 (the phone's captive portal opens it)
  APP_WEBSHARE_HOME = 2, // on the saved home network, http://writer.local (2.4 GHz only, like net_up)
} app_webshare_mode_t;

typedef struct {
  bool running;
  int32_t mode; // APP_WEBSHARE_HOME or APP_WEBSHARE_AP: what is in use
  char code[8]; // the 6 digits to type on the page, NUL-terminated
  char network[33]; // AP mode: the network to join ("Writer"); empty otherwise
  char address[40]; // what to open: "http://4.3.2.1" (AP) or "http://writer.local" (HOME)
  char ip[16]; // the X's address (4.3.2.1 for the access point)
  char url[32]; // "http://<ip>/": show it as "Online at ..."
  char login_url[72]; // "http://<ip>/?t=<128-bit token>": for the QR. One login only, gone at stop or after 15 min;
                      // never log it. Typing the 6-digit code works as well.
  uint32_t idle_left_s; // seconds until it switches itself off
  int32_t locked; // 0 usable; 1 too many wrong codes, locked for a while; 2 too many in all: stopped (start again)
} app_webshare_status_t;

// The "files" profile of the Wi-Fi page (API 2.3, webshare_start_files): the page lists, adds and deletes the app's own
// files of one folder instead of documents and fonts. Only files whose name ends in one of `exts` are listed or
// taken, none over `max_bytes`; the same rules as above (temporary file, free-space reserve, nothing overwritten, the
// 6-digit code, delete only after the phone's confirm). The page also has the touch bar's buttons.
typedef struct {
  const char *dir; // inside the app's folder: "" (the folder itself), "roms" or "a/b" (two names at most, no "..")
  const char *exts; // comma separated, with the dot, e.g. ".gb,.gbc" (at most 8, letters and digits, any case)
  uint32_t max_bytes; // the largest file, 1 .. 33554432
} app_webshare_files_t;

// ---- settings form (API 2.6) ----

// One field of the app's settings form (settings_declare): the phone shows it on the Wi-Fi page in the "settings"
// profile (webshare_start_settings); a saved value is checked against it and written with setting_set under `key`.
// Values are stored as text: a number as its decimal digits ("-12"), a choice as the choice itself. An empty text or
// secret (allowed with min 0) removes the key (setting_get then gives APP_ENOENT)
typedef enum {
  APP_SETTING_TEXT = 0, // min..max bytes of UTF-8 (max 1..256)
  APP_SETTING_SECRET = 1, // as text, never shown back on the page (only "set" / "not set") and never logged
  APP_SETTING_NUMBER = 2, // a whole number in min..max
  APP_SETTING_CHOICE = 3, // one of `choices`
} app_setting_kind_t;

typedef struct {
  const char *key; // setting_get/set key: 1..15 of A-Z a-z 0-9 _
  const char *label; // shown on the phone, 1..48 bytes, any language
  const char *choices; // CHOICE: "one|two|three" (up to 16, 256 bytes in all); NULL otherwise
  int32_t min, max; // TEXT, SECRET: bytes; NUMBER: the value
  uint32_t kind; // app_setting_kind_t
} app_setting_decl_t;

// ---- local time (API 2) ----

typedef struct {
  int16_t year; // e.g. 2026
  uint8_t month; // 1..12
  uint8_t day; // 1..31
  uint8_t hour, minute, second;
  uint8_t weekday; // 0 = Sunday
  int32_t utc_offset_s; // local minus UTC
  uint8_t zone_known; // 0: the X has no time zone: the fields are UTC (utc_offset_s 0)
  uint8_t dst; // daylight saving time is in effect
  uint16_t reserved;
} app_local_time_t;

// ---- ESP-NOW (API 2.4) ----

#define APP_ESPNOW_MAX 250 // ESP-NOW v1 payload limit

typedef struct {
  uint8_t src[6]; // the sender's MAC
  int8_t rssi; // of the frame, dBm
  uint8_t len;
  uint8_t data[APP_ESPNOW_MAX];
} app_espnow_pkt_t;

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
#define APP_EBUSY -8 // the USB drive is showing the app's files (version 2)
#define APP_ENOSPC -9 // no room: the drive cannot hold the files, or the X cannot hold the computer's
#define APP_ENOCLOCK -10 // the clock was never set (no network since the battery was empty)

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

  // ---- API 2 ----

  // Keyboards (docs/apps.md "Keyboards"): `sources` APP_KBD_* to use, 0 to stop. USB: the port becomes a host
  // only while no charger or computer powers it; Bluetooth: a pairing screen if no keyboard is bonded yet.
  // Their keys come as KEY_DOWN / KEY_UP with hid_usage. Returns 0, APP_EINVAL (unknown bits) or APP_ENOMEM
  int (*keyboard_enable)(uint32_t sources);
  // The APP_KBD_* bits of the keyboards connected now
  uint32_t (*keyboard_status)(void);

  // USB drive (docs/apps.md "USB drive"). start() exports the files and shows the drive (0; APP_EBUSY if shown,
  // APP_ENOMEM, APP_ENOSPC, APP_EIO). It ends by itself on eject, unplug or timeout (0: 60 s);
  // stop() ends it if still on, then fills *res with the last showing's result and returns its
  // result. The import keeps both copies of a file changed on both sides.
  int (*usb_drive_start)(uint32_t connect_timeout_ms);
  int (*usb_drive_state)(void); // app_drive_state_t
  int (*usb_drive_stop)(app_drive_result_t *res);

  // Wi-Fi page (docs/apps.md "Wi-Fi page"). start returns 0 or APP_ENONET (HOME asked and no saved 2.4 GHz network reachable), APP_ENOMEM, APP_EIO,
  // APP_EINVAL (bad mode); starting again makes a new code. status fills *st (also when stopped) and returns 0.
  int (*webshare_start)(app_webshare_mode_t mode);
  void (*webshare_stop)(void);
  int (*webshare_status)(app_webshare_status_t *st);

  // Battery: the charge in percent (0..100) as the fuel gauge read it at the last wake, or APP_EIO when it could
  // not be read. *charging (may be NULL): the charger is charging now
  int (*battery_percent)(bool *charging);
  // Local time from the clock and the X's time zone. 0 with *out filled (out->zone_known 0: no time zone is set,
  // the fields are UTC), APP_ENOCLOCK, or APP_EINVAL (out NULL)
  int (*local_time)(app_local_time_t *out);

  // ---- API 2.1 (api->size >= APP_API_SLOT(35)) ----

  // The X's event log (docs/apps.md "Event log"): the last `n` lines, oldest first, each ending in '\n', NUL-terminated
  // in buf ("#<boot> <uptime>s <UTC time or -> <event>[ (xN)]"); fewer when they do not fit. Returns the lines,
  // APP_EINVAL (buf NULL or len 0) or APP_ENOMEM
  int (*event_log_lines)(char *buf, size_t len, uint32_t n);

  // ---- API 2.2 (api->size >= APP_API_SLOT(36)) ----

  // TRMNL's orientation, from its last image (TRMNL keeps no setting on the device: the server sends a portrait screen
  // as an image with the sides swapped): APP_ORIENT_LANDSCAPE, APP_ORIENT_PORTRAIT, or APP_ENOENT when no TRMNL image
  // was drawn since the last cold start. Since 2.5 the accelerometer's once it has a reading: BAR_BOTTOM and BAR_TOP
  // as APP_ORIENT_LANDSCAPE, BAR_LEFT and BAR_RIGHT as APP_ORIENT_PORTRAIT (device_orientation tells which)
  int (*screen_orientation)(void);

  // ---- API 2.3 (api->size >= APP_API_SLOT(37)) ----

  // webshare_start for the files profile (app_webshare_files_t above); webshare_stop and webshare_status work as for the
  // writer page. Returns what webshare_start does, APP_EINVAL also for files that are NULL or not valid. The strings are
  // copied
  int (*webshare_start_files)(app_webshare_mode_t mode, const app_webshare_files_t *files);

  // ---- API 2.4 (api->size >= APP_API_SLOT(41)) ----

  // ESP-NOW receive (docs/apps.md "ESP-NOW"): the station MAC becomes `mac` with bit 0 of byte 0 cleared (as the ELRS
  // backpack sets its UID), Wi-Fi off any network, on `channel` (1..13); every ESP-NOW frame that arrives is queued
  // (64 frames; more are dropped and counted). Returns 0, APP_EINVAL (mac NULL or all zero, bad channel), APP_EBUSY
  // (the Wi-Fi page, net_up's network, a Bluetooth keyboard or ESP-NOW itself holds the radio), APP_ENOMEM or APP_EIO.
  // While it runs net_up fails and webshare_start, webshare_start_files and keyboard_enable(APP_KBD_BLE) return
  // APP_EBUSY. The MAC is never logged.
  int (*espnow_start)(const uint8_t mac[6], uint8_t channel);
  // Takes one queued frame: 1 with *pkt filled, 0 when none waits or ESP-NOW is off (never blocks), APP_EINVAL (NULL)
  int (*espnow_recv)(app_espnow_pkt_t *pkt);
  // Ends it: the original MAC and protocol back, the radio off. Returns the frames dropped since start (0 when off).
  // The firmware calls it when the app ends
  int (*espnow_stop)(void);
  // `zones`: 1 << APP_TOUCH_MIDDLE to get APP_GESTURE_HOLD for the middle, 0 to stop (the default: a slow press is a
  // tap). Other bits: APP_EINVAL. The long press (exit) is unchanged
  int (*touch_hold)(uint32_t zones);

  // ---- API 2.5 (api->size >= APP_API_SLOT(44)) ----

  // The X's position from its accelerometer (APP_ORIENT_BAR_*): the last one held steady; lying flat keeps the last.
  // Without a reading yet (flat since the start, or no sensor): TRMNL's from its last image, else APP_ORIENT_BAR_BOTTOM
  int (*device_orientation)(void);
  // on: the app draws in device_orientation's position. Touch zones then come in the user's frame (APP_TOUCH_LEFT is
  // the zone on the user's left, on the top in a portrait position; the middle stays the middle) and an
  // APP_EVENT_ORIENTATION comes once a new position held for settle_ms (0: APP_ORIENT_SETTLE_MS). off (the default):
  // the zones of the panel's own frame, no events. Returns the position now, or APP_EINVAL (settle_ms over 10000)
  int (*orientation_follow)(bool on, uint32_t settle_ms);
  // Fills *out with where the touch bar is in position `orientation`. 0, or APP_EINVAL (bad position, out NULL)
  int (*touchbar_layout)(int orientation, app_touchbar_t *out);

  // ---- API 2.6 (api->size >= APP_API_SLOT(47)) ----

  // The X's keyboard layout, for the app's help text: "Belgian AZERTY", "French AZERTY", "US QWERTY", "UK QWERTY",
  // "German QWERTZ". The user picks it on the Wi-Fi page (any profile); keys come mapped by it after keyboard_chars(1)
  const char *(*keyboard_layout_name)(void);
  // Declares the app's settings form (app_setting_decl_t above; copied, n 0 clears it). 0 or APP_EINVAL (a bad field,
  // more than 16). Kept until the app ends
  int (*settings_declare)(const app_setting_decl_t *decls, uint32_t n);
  // The Wi-Fi page in the "settings" profile: the declared form, the keyboard layout, the touch bar's buttons. The same
  // login, code and status as webshare_start; APP_EVENT_SETTINGS after each save that changed something. Returns what
  // webshare_start does, APP_ENOENT when nothing is declared, APP_EBUSY while ESP-NOW holds the radio (espnow_stop
  // first)
  int (*webshare_start_settings)(app_webshare_mode_t mode);
  // 1: a USB or Bluetooth key with a character on the X's layout comes as APP_KEY_CHAR with utf8 (hid_usage kept);
  // 0 (the default at every start): APP_KEY_HID as in API 2.0, for an app with its own layouts. Check SLOT(48) first
  int (*keyboard_chars)(int on);
} app_api_t;

#define APP_ORIENT_SETTLE_MS 1000

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define APP_API_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define APP_API_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

// The event is copied into apps built for API 1: its size and offsets stay
APP_API_ASSERT(sizeof(app_event_t) == 12, "app_event_t is part of the app ABI");

// The table only grows: each version's calls at fixed places (P = a pointer: 104 / 144 bytes on the X).
// API 1: 24 calls, screen_width .. exit. API 2: keyboards (2), USB drive (3), Wi-Fi page (3), battery and time (2)
#define APP_API_SLOT(n) (8 + (n) * sizeof(void *))
APP_API_ASSERT(offsetof(app_api_t, screen_width) == APP_API_SLOT(0), "API 1 starts after version and size");
APP_API_ASSERT(offsetof(app_api_t, exit) == APP_API_SLOT(23), "API 1 has 24 calls");
APP_API_ASSERT(offsetof(app_api_t, keyboard_enable) == APP_API_SLOT(24), "API 2 starts right after API 1");
APP_API_ASSERT(offsetof(app_api_t, usb_drive_start) == APP_API_SLOT(26), "API 2: keyboards, then the USB drive");
APP_API_ASSERT(offsetof(app_api_t, webshare_start) == APP_API_SLOT(29), "API 2: then the Wi-Fi page");
APP_API_ASSERT(offsetof(app_api_t, battery_percent) == APP_API_SLOT(32), "API 2: then battery and local time");
APP_API_ASSERT(offsetof(app_api_t, event_log_lines) == APP_API_SLOT(34), "API 2.1: the event log after API 2");
APP_API_ASSERT(offsetof(app_api_t, screen_orientation) == APP_API_SLOT(35), "API 2.2: orientation after the event log");
APP_API_ASSERT(offsetof(app_api_t, webshare_start_files) == APP_API_SLOT(36), "API 2.3: the files profile after orientation");
APP_API_ASSERT(offsetof(app_api_t, espnow_start) == APP_API_SLOT(37), "API 2.4: ESP-NOW after the files profile");
APP_API_ASSERT(offsetof(app_api_t, touch_hold) == APP_API_SLOT(40), "API 2.4: then the touch hold");
APP_API_ASSERT(offsetof(app_api_t, device_orientation) == APP_API_SLOT(41), "API 2.5: orientation after the touch hold");
APP_API_ASSERT(offsetof(app_api_t, keyboard_layout_name) == APP_API_SLOT(44), "API 2.6: layout after touchbar_layout");
APP_API_ASSERT(sizeof(app_api_t) == APP_API_SLOT(48), "API 2.6 has 48 calls: a new one raises APP_API_MINOR");
APP_API_ASSERT(sizeof(app_touchbar_t) == 32, "app_touchbar_t is part of the app ABI");
APP_API_ASSERT(sizeof(app_espnow_pkt_t) == 258, "app_espnow_pkt_t is part of the app ABI");
APP_API_ASSERT(sizeof(app_local_time_t) == 16, "app_local_time_t is part of the app ABI");
APP_API_ASSERT(sizeof(app_setting_decl_t) == 3 * sizeof(void *) + 12 + (sizeof(void *) == 8 ? 4 : 0),
               "app_setting_decl_t is part of the app ABI");
APP_API_ASSERT(APP_API_VERSION == 2 && APP_API_MINOR == 6, "a new version adds its own size check above");

#endif // APP_API_H
