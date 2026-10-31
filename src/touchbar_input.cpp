// The touch bar's one input path while awake (touchbar_input.h).
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <iqs323_task.h>
#include <touch_hold.h>
#include <touch_inject.h>
#include <touchbar_input.h>
#include <trmnl_log.h>

typedef struct {
  bool down;
  uint32_t since;
  bool long_press; // reported, or a touch held at begin: no tap, no long press for it
} touch_state_t;

static touch_state_t touch[3];
static const char *consumer_name = "none";
static volatile uint32_t hold_zones; // set from the app's task, read by the runner's loop

void touchbar_input_set_hold(uint32_t zones) { hold_zones = zones; }

const char *touch_gesture_str(touch_gesture_t g) {
  switch (g) {
  case touch_gesture_t::DOWN:
    return "down";
  case touch_gesture_t::TAP:
    return "tap";
  case touch_gesture_t::LONG:
    return "long press";
  case touch_gesture_t::HOLD:
    return "hold";
  }
  return "?";
}

void touchbar_input_begin(const char *consumer) {
  consumer_name = consumer;
  iqs323_task_set_streaming_mode(true); // fresh touch data while awake; also republishes the status (iqs323_task.h)
  iqs323_task_take_touch_starts();      // the touch that led here
  touch_inject_clear();                 // web presses from an earlier screen
  touchbar_snapshot_t snap;
  iqs323_task_read_snapshot(&snap);
  const bool down[3] = {snap.ch0_touch, snap.ch1_touch, snap.ch2_touch};
  uint32_t now = millis();
  for (int ch = 0; ch < 3; ch++) touch[ch] = {down[ch], now, down[ch]};
  Log_info("Touch: input to %s (held: %d%d%d)", consumer, down[0], down[1], down[2]);
}

void touchbar_input_end(void) {
  iqs323_task_set_streaming_mode(false);
  consumer_name = "none";
  hold_zones = 0;
}

static void add(touch_event_t *out, int *n, int ch, touch_gesture_t g, uint32_t ms) {
  Log_info("Touch: iqs323 ch%d %s (%lu ms) -> %s", ch, touch_gesture_str(g), (unsigned long)ms, consumer_name);
  if (*n < TOUCHBAR_INPUT_MAX) out[(*n)++] = {(uint8_t)ch, g};
}

int touchbar_input_poll(touch_event_t *out) {
  touchbar_snapshot_t snap;
  iqs323_task_read_snapshot(&snap);
  uint8_t starts = iqs323_task_take_touch_starts();
  const bool now_down[3] = {snap.ch0_touch, snap.ch1_touch, snap.ch2_touch};
  uint32_t now = millis();
  int n = 0;
  for (int ch = 0; ch < 3; ch++) {
    touch_state_t &t = touch[ch];
    // A new touch start always begins a new touch: a stale "down" must never swallow later presses
    if ((starts & (1 << ch)) || (!t.down && now_down[ch])) {
      t = {true, now, false};
      add(out, &n, ch, touch_gesture_t::DOWN, 0);
    }
    if (!t.down) continue;
    if (now_down[ch]) {
      if (!t.long_press && now - t.since >= TOUCHBAR_LONG_PRESS_MS) {
        t.long_press = true;
        add(out, &n, ch, touch_gesture_t::LONG, now - t.since);
      }
      continue;
    }
    // released (or a tap too short for the snapshot to show it down)
    if (!t.long_press) add(out, &n, ch, (touch_gesture_t)touch_release_gesture(ch, now - t.since, hold_zones), now - t.since);
    t.down = false;
  }
  // presses from the Install via Wi-Fi page: the same events as the touch bar's (touch_inject.h)
  touch_inject_ev_t web[TOUCH_INJECT_MAX];
  int w = touch_inject_take(web, TOUCH_INJECT_MAX);
  for (int i = 0; i < w; i++) add(out, &n, web[i].channel, (touch_gesture_t)web[i].gesture, 0);
  return n;
}

#endif // BOARD_TRMNL_X
