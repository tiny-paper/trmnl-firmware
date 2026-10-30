#include "drive_session.h"

void drive_session_reset(drive_session_t *s) { *s = {false, 0, 0, false}; }

void drive_session_wrote(drive_session_t *s, uint32_t now_ms) {
  s->writes++;
  s->last_write_ms = now_ms;
  s->synced = false;
}

void drive_session_synced(drive_session_t *s) {
  if (s->writes) s->synced = true;
}

bool drive_session_take(const drive_session_t *s, drive_end_t end, uint32_t now_ms) {
  if (!s || s->writes == 0) return false; // nothing written: nothing to take
  if (end == DRIVE_END_EJECTED || s->synced) return true;
  return (uint32_t)(now_ms - s->last_write_ms) >= DRIVE_QUIET_MS; // wraps with millis()
}
