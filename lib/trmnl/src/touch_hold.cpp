#include <touch_hold.h>

touch_release_t touch_release_gesture(int channel, uint32_t held_ms, uint32_t hold_zones) {
  if (channel < 0 || channel > 2 || !(hold_zones & TOUCH_HOLD_ZONES & (1u << channel))) return TOUCH_RELEASE_TAP;
  return held_ms >= TOUCH_HOLD_MS ? TOUCH_RELEASE_HOLD : TOUCH_RELEASE_TAP;
}
