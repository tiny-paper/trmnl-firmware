#pragma once

// The event log's crash record (src/evlog_crash.cpp, docs/apps.md "Event log"): in RTC memory, so a panic or watchdog
// reset keeps it. It holds the events not yet in the file, the running app with its entry address, and what the panic
// handler saw (reason, PC, first backtrace addresses). The next boot puts them into the log (evlog.cpp).
#ifdef BOARD_TRMNL_X

#include <evlog_ring.h>
#include <stddef.h>
#include <stdint.h>

#define EVLOG_CRASH_PENDING 6 // the newest events not yet written
#define EVLOG_CRASH_BT 4 // backtrace addresses kept

typedef struct {
  uint16_t boot;
  uint32_t ms, wall;
  char text[EVLOG_TEXT_MAX + 1];
} evlog_pending_t;

// An event added to the log and not yet in the file / all of them written
void evlog_crash_pending(uint16_t boot, uint32_t ms, uint32_t wall, const char *text);
void evlog_crash_written(void);

// The app now running (its name and entry address), NULL when it ended
void evlog_crash_app(const char *name, uint32_t entry);

// At boot: the pending events (oldest first, up to `max`) and, after a crash (`crashed`), its line "crash: ..." in
// `crash` ("" otherwise); *boot is the boot they belong to. Clears the record. Returns the events
int evlog_crash_take(bool crashed, evlog_pending_t *out, int max, char *crash, size_t crash_len, uint16_t *boot);

// Hooks the panic handler (once, at boot)
void evlog_crash_begin(void);

// The boot now running (after evlog_ring_new_boot)
void evlog_crash_set_boot(uint16_t boot);

#endif
