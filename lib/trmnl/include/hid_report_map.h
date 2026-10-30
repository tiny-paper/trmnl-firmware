#pragma once

#include <stddef.h>
#include <stdint.h>

// The keyboard input report of a HID report map (a Bluetooth keyboard's Report Map characteristic, HOGP), and
// its reports turned into the 8-byte boot format hid_keys.h takes. Only what a keyboard needs: the modifier bits,
// a key array or a key bitmap, in an Application collection with usage Generic Desktop / Keyboard.

#define HID_MAP_MAX_LEN 512 // HOGP: a Report Map is at most 512 bytes

typedef struct {
  bool found;
  uint8_t report_id; // 0: the map uses no report IDs
  int16_t mod_bit; // bit offset of the 8 modifier bits (usages 0xE0..0xE7), -1 none
  int16_t keys_bit; // bit offset of the key array (8-bit usages), -1 none
  uint8_t keys_count;
  int16_t keys_base; // usage = value + keys_base (usage minimum less logical minimum, mostly 0)
  int16_t bitmap_bit; // bit offset of a key bitmap (1 bit per usage), -1 none
  uint8_t bitmap_min; // usage of its first bit
  uint8_t bitmap_count;
  uint16_t report_bits; // length of the report (without the report ID byte)
} hid_map_kbd_t;

// Parses `map`; false if it holds no keyboard input report (a mouse, a bad or cut-off map)
bool hid_map_parse(const uint8_t *map, size_t len, hid_map_kbd_t *out);

// One input report of that report ID (without the ID byte, as HOGP notifies it) into the boot format: byte 0
// modifiers, 2..7 up to 6 usages. More than 6 keys of a bitmap: ErrorRollOver (usage 1) as a boot keyboard sends.
// False if the report is shorter than the layout says.
bool hid_map_to_boot(const hid_map_kbd_t *k, const uint8_t *report, size_t len, uint8_t boot[8]);
