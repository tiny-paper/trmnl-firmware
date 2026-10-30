#include "serial_watch.h"

#include <string.h>

void serial_watch_init(serial_watch_t *w) { memset(w, 0, sizeof(*w)); }

serial_watch_act_t serial_watch_step(serial_watch_t *w, uint32_t now_ms, int free, bool plugged) {
  if (free > w->max_free) w->max_free = free;
  bool grew = w->started && free > w->last_free;
  bool empty = free >= w->max_free;
  w->started = true;
  w->last_free = free;
  if (!plugged) { // no host: dropping is HWCDC's right; a stall counts from the host's return
    w->since = now_ms;
    w->kicks = 0;
    return SERIAL_WATCH_NONE;
  }
  if (grew || empty) {
    serial_watch_act_t a = SERIAL_WATCH_NONE;
    if (w->kicks) {
      w->stall_ms = now_ms - w->since;
      w->resumed_kicks = w->kicks;
      a = SERIAL_WATCH_RESUMED;
    }
    w->since = now_ms;
    w->kicks = 0;
    return a;
  }
  if (now_ms - w->since < SERIAL_WATCH_KICK_MS) return SERIAL_WATCH_NONE;
  if (w->kicks && now_ms - w->last_kick < SERIAL_WATCH_KICK_MS) return SERIAL_WATCH_NONE;
  w->last_kick = now_ms;
  w->kicks++;
  return SERIAL_WATCH_KICK;
}
