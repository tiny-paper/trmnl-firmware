#pragma once
// The X's USB serial output watchdog (rule: lib/trmnl/include/serial_watch.h): a task that kicks HWCDC when its TX
// ring stops draining while a host is there, and logs when the output resumes. After Serial.begin.
#ifdef BOARD_TRMNL_X
void serial_watch_start(void);
#else
static inline void serial_watch_start(void) {}
#endif
