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

// ---- input ----

typedef enum {
  APP_EVENT_NONE,
  APP_EVENT_KEY_DOWN, // a key (USB or serial keyboard)
  APP_EVENT_KEY_UP,
  APP_EVENT_TOUCH, // a touch bar gesture
  APP_EVENT_EXIT, // the user asked to leave (long press): save and return within APP_EXIT_GRACE_MS
  APP_EVENT_KEYBOARD, // a keyboard came or went: `key` holds the APP_KBD_* bits connected now (API 2)
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
} app_gesture_t;

typedef struct {
  uint8_t type; // app_event_type_t
  uint8_t modifiers; // APP_MOD_*
  uint16_t key; // app_key_t, for KEY_DOWN / KEY_UP
  char utf8[5]; // the character typed, NUL-terminated; empty for non-printable keys
  uint8_t zone; // app_touch_zone_t, for TOUCH
  uint8_t gesture; // app_gesture_t, for TOUCH
  // For keys of a USB or Bluetooth keyboard: the HID usage (keyboard page, 0x04 = the key labelled A on a US
  // layout), 0 otherwise. The firmware maps no layout: Enter, Escape, Backspace, Tab, Delete, the arrows, Home,
  // End, Page Up/Down and F1..F12 come with their app_key_t, every other key as APP_KEY_HID (API 2)
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
  int status; // HTTP status, or < 0 for a transport error (no Wi-Fi, DNS, TLS, timeout)
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

// A page on the X's own Wi-Fi network "Writer" (with a captive portal) or on the home network at writer.local, where
// a phone browser lists, downloads (one file or a zip), uploads, renames and deletes the app's `documents/` files
// and adds fonts to `settings/fonts/`. Nothing is ever overwritten: an upload that meets a different file of the
// same name is kept next to it as "name (web N).ext". The page is off until the app starts it, and off again when
// the app ends or after APP_WEBSHARE_IDLE_S without a request. A 6-digit code, shown by the app, must be typed on
// the page first (docs/apps.md).
typedef enum {
  APP_WEBSHARE_AUTO = 0, // the default: the saved home network, so the phone stays on its Wi-Fi; only when none is
                         // reachable the access point below (webshare_status says which)
  APP_WEBSHARE_AP = 1, // own access point "Writer", open, address http://4.3.2.1 (the phone's captive portal opens it)
  APP_WEBSHARE_HOME = 2, // on the saved home network, http://writer.local (2.4 GHz only, like net_up)
} app_webshare_mode_t;

#define APP_WEBSHARE_IDLE_S (15 * 60)

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
  // Their keys come as KEY_DOWN / KEY_UP with hid_usage. Returns 0, or APP_ENOTSUP for a source the firmware lacks
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
} app_api_t;

#ifdef __cplusplus
}
#endif

// The event is copied into apps built for API 1: its size and offsets stay
#ifdef __cplusplus
static_assert(sizeof(app_event_t) == 12, "app_event_t is part of the app ABI");
#else
_Static_assert(sizeof(app_event_t) == 12, "app_event_t is part of the app ABI");
#endif

#endif // APP_API_H
