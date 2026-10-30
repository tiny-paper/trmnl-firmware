#pragma once

#include <stddef.h>
#include <stdint.h>

#include <app_api.h>

// Key events (app_api.h) from the input reports of a USB or Bluetooth keyboard in the boot report format:
// byte 0 the modifier bits, byte 1 reserved, bytes 2..7 the usages of the keys held. No layout is mapped:
// the app gets the HID usage (app_event_t.hid_usage).

#define HID_KEYS_REPORT   8
#define HID_KEYS_MAX_EVENTS 12 // 6 released, 6 pressed

typedef struct {
  uint8_t held[HID_KEYS_REPORT]; // the last report taken
} hid_keys_t;

void hid_keys_init(hid_keys_t *k);

// Takes one report (`len` bytes; shorter than HID_KEYS_REPORT is ignored, longer is cut): fills `out` with
// KEY_UP for keys released, then KEY_DOWN for keys pressed; returns how many. A phantom report (usage 1,
// too many keys at once) gives none and keeps the last state.
int hid_keys_report(hid_keys_t *k, const uint8_t *report, size_t len, app_event_t out[HID_KEYS_MAX_EVENTS]);

// The keyboard went away (unplugged, out of range): KEY_UP for every key still held, so none stays down
int hid_keys_release_all(hid_keys_t *k, app_event_t out[HID_KEYS_MAX_EVENTS]);

// app_key_t for a usage: the layout independent keys, else APP_KEY_HID
uint16_t hid_keys_code(uint8_t usage);

// APP_MOD_* from the report's modifier byte
uint8_t hid_keys_mods(uint8_t hid_mods);
