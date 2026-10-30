#pragma once

// The X's event log (docs/apps.md "Event log"): recent keyboard, USB port, app and network events that survive a
// restart and deep sleep, in /evlog.bin (EVLOG_BYTES, the ring of lib/trmnl/include/evlog_ring.h). Writes only on
// events and at most every EVLOG_FLUSH_GAP_MS; a known restart or sleep writes at once. Read out with the serial
// command "TAPP EVLOG" while an app runs, or by an app (api->event_log_lines).

#include <stddef.h>
#include <stdint.h>

#ifdef BOARD_TRMNL_X

#include <trmnl_log.h>

// After the file system is mounted: reads the file back, counts a new boot, logs a reset that was not a deep sleep wake
void evlog_begin(void);

// One event, printf-style (any task; not from an interrupt). Same text as the last one: counted up, not added
void evlog_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Writes the log now if it changed (before a restart); `last` true also before sleep: no write after it
void evlog_flush(bool last);

// Both the serial log and the event log
#define Log_event(format, ...)                                                                                         \
  do {                                                                                                                 \
    Log_info(format, ##__VA_ARGS__);                                                                                   \
    evlog_add(format, ##__VA_ARGS__);                                                                                  \
  } while (0)

// "TAPP EVLOG" (all, oldest first, then "TAPP EVLOG END") and "TAPP EVLOG CLEAR"; true when `line` was one of them
bool evlog_serial_command(const char *line);

// api->event_log_lines: the last `n` lines into buf (evlog_ring_tail)
int evlog_tail(char *buf, size_t len, uint32_t n);

#else

static inline void evlog_add(const char *fmt, ...) { (void)fmt; }
static inline void evlog_flush(bool last) { (void)last; }

#endif
