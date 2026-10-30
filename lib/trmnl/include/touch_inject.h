#pragma once

#include <stddef.h>
#include <stdint.h>

// Touch events from the Install via Wi-Fi page's buttons (Left / Middle / Right / Long press / Stop server). They
// enter the same path as the touch bar (touchbar_input_poll appends them), so a screen cannot tell a web press from a
// finger. A single producer (the web server task) and a single consumer (the screen's loop).

#define TOUCH_INJECT_MAX 8

// Channels 0 left, 1 middle, 2 right; gestures as touch_gesture_t (0 down, 1 tap, 2 long)
typedef struct {
  uint8_t channel;
  uint8_t gesture;
} touch_inject_ev_t;

// The page's request: `button` is "left", "middle", "right", "long" or "stop" ("stop" is a long press of the middle).
// False for anything else, nothing queued.
bool touch_inject_button(const char *button);
// False when the queue is full (the event is dropped)
bool touch_inject_push(uint8_t channel, uint8_t gesture);
// Moves up to `max` queued events to `out`, oldest first; returns how many
int touch_inject_take(touch_inject_ev_t *out, int max);
// Forgets all queued events (a screen starts reading)
void touch_inject_clear(void);
