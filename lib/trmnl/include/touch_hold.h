#pragma once

#include <stdint.h>

// A touch bar press released before the long press (TOUCHBAR_LONG_PRESS_MS, which ends an app): a tap, or for an app
// that asked (touch_hold, API 2.4) a hold once it lasted APP_TOUCH_HOLD_MS. Pure: src/touchbar_input.cpp uses it.

#define TOUCH_HOLD_MS 600 // APP_TOUCH_HOLD_MS
#define TOUCH_HOLD_ZONES 0x02u // the zones that can hold: only the middle (1 << APP_TOUCH_MIDDLE)

typedef enum { TOUCH_RELEASE_TAP = 1, TOUCH_RELEASE_HOLD = 3 } touch_release_t; // touch_gesture_t TAP, HOLD

// `channel` 0 left, 1 middle, 2 right; `hold_zones` the bits the app asked for (0: every release is a tap)
touch_release_t touch_release_gesture(int channel, uint32_t held_ms, uint32_t hold_zones);
