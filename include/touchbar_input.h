#pragma once

#include <stdint.h>

// The one input path from the TRMNL X touch bar (IQS323 task snapshot and touch starts, iqs323_task.h) to everything
// that reads it while awake: TRMNL's menu and tutorial, the firmware screens (Install via Wi-Fi, Receive over USB,
// the Bluetooth pairing screens) and the running app. Each event is logged once:
// "Touch: iqs323 chN <gesture> -> <consumer>".

enum class touch_gesture_t : uint8_t {
  DOWN, // a touch started (the menu acts on it)
  TAP,  // released before the long press time (apps and firmware screens act on it)
  LONG, // held for TOUCHBAR_LONG_PRESS_MS (ends an app or a firmware screen)
};

struct touch_event_t {
  uint8_t channel; // 0 left, 1 middle, 2 right
  touch_gesture_t gesture;
};

#define TOUCHBAR_LONG_PRESS_MS 1500
#define TOUCHBAR_INPUT_MAX     9 // events one poll can return: three per channel

// Starts reading for `consumer` (a name for the log): streaming mode on, fresh status read; a touch already down
// (the tap that opened the menu, chose the app or woke the device) is not reported.
void touchbar_input_begin(const char *consumer);

// Back to event mode (TRMNL's sleep and wake rules).
void touchbar_input_end(void);

// The events since the last call, oldest first; returns how many (up to TOUCHBAR_INPUT_MAX).
int touchbar_input_poll(touch_event_t *out);

const char *touch_gesture_str(touch_gesture_t g);
