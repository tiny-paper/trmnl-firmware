#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <app_api.h>

// The X's keyboard layout (API 2.6, docs/apps.md "Keyboard layout"): one setting of the device, chosen on the Wi-Fi
// page. A key of a USB or Bluetooth keyboard (HID usage and modifiers) becomes the character that key types on that
// layout; the firmware hands it to the app as APP_KEY_CHAR with utf8, the usage stays in hid_usage. Keys without a
// character (and every key on an unknown layout) stay APP_KEY_HID. Dead keys type their accent at once (not
// combined); Caps Lock is not applied (boot reports carry no lock state). Tables: the Belgian one from trmnl-writer's
// kbmap (MIT, ExcuseMi), the others in the same shape.

typedef enum {
  KBD_LAYOUT_BE = 0, // Belgian AZERTY, the default
  KBD_LAYOUT_FR, // French AZERTY
  KBD_LAYOUT_US, // US QWERTY
  KBD_LAYOUT_UK, // UK QWERTY
  KBD_LAYOUT_DE, // German QWERTZ
  KBD_LAYOUT_COUNT,
} kbd_layout_t;

#define KBD_LAYOUT_DEFAULT KBD_LAYOUT_BE
#define KBD_LAYOUT_NVS_KEY "kbd_layout" // NVS key (TRMNL's namespace) holding the layout's code

// "Belgian AZERTY", ...; the default's name for a value out of range
const char *kbd_layout_name(int layout);
// The short code kept in NVS and used by the page: "be", "fr", "us", "uk", "de"
const char *kbd_layout_code(int layout);
// The layout of a code, or -1 for an unknown one (NULL too)
int kbd_layout_from_code(const char *code);

// The Unicode character of `usage` with APP_MOD_* `mods` on `layout`: Shift picks the second level, AltGr (APP_MOD_ALTGR,
// without Shift) the third; Ctrl, Alt and Meta do not change it. 0 when the key types nothing there
uint32_t kbd_layout_char(int layout, uint8_t usage, uint8_t mods);

// UTF-8 of cp into out (NUL-terminated, 5 bytes); the byte count, 0 for cp 0 or over U+FFFF
int kbd_utf8(uint32_t cp, char out[5]);

// A KEY_DOWN or KEY_UP from a keyboard (key APP_KEY_HID): a key with a character on `layout` becomes APP_KEY_CHAR with
// utf8 filled. Other events and keys are left as they are. True when it changed the event
bool kbd_layout_apply(int layout, app_event_t *ev);
// kbd_layout_apply only for an app that asked (keyboard_chars, API 2.6): an app that maps HID usages itself (the writer
// with its own layout setting) gets APP_KEY_HID as before, or it would map a mapped character a second time
bool kbd_layout_apply_for_app(bool app_wants_chars, int layout, app_event_t *ev);
