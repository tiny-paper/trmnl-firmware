#pragma once

// The touch bar hint pills along the bottom edge: the one drawing used by TRMNL's menu and tutorial (menu.cpp) and by
// the firmware screens (Install via Wi-Fi, Receive over USB, Bluetooth keyboard). Slots 0, 1, 2 sit over the touch
// bar's left, middle and right regions. Draws into bbep; the caller updates the panel. 1-bpp mode (white is BBEP_WHITE).
#ifdef BOARD_TRMNL_X

#include <stdint.h>

enum hints_icon_t { // same values as menu_hint_icon_t
  HINTS_ICON_NONE,
  HINTS_ICON_UP,
  HINTS_ICON_DOWN,
};

enum hints_style_t {
  HINTS_PLAIN,    // the label alone
  HINTS_OUTLINED, // in an outlined pill
  HINTS_FILLED,   // white in a black pill
};

#define HINTS_MARGIN 40 // from the bottom of the screen to the hint baselines

// Center x of a slot (0..2)
int hints_slot_x(int slot);
// A triangle pointing right (dir 0), up (1) or down (2) centered on (cx, cy); `size` along the base
void hints_fill_triangle(int cx, int cy, int size, int dir, uint8_t color);
// One hint: an up/down triangle if `icon`, then `label`, in `style`
void hints_draw(const char *label, hints_icon_t icon, int slot, hints_style_t style);
// The same on a row of its own: `baseline` is the label's baseline (hints_draw: HINTS_MARGIN above the bottom edge)
void hints_draw_at(const char *label, hints_icon_t icon, int slot, hints_style_t style, int baseline);
// The long press hint every firmware screen shows: "Long press: <what>" under the touch bar's middle
void hints_draw_long_press(const char *what, int baseline);

#endif
