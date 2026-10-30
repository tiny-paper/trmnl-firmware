#pragma once
// Watchdog rule for the X's USB serial output (Arduino HWCDC, cores/esp32/HWCDC.cpp 3.3.7). HWCDC moves its TX ring
// into the USB FIFO only from its IN_EMPTY interrupt. Its ISR masks that interrupt before it reads the ring, and only
// write()/flush() unmask it again, and only while its own `connected` flag is true; once it judges the host gone
// (write or flush timeout, bus reset, a missed SOF) the unmask happens once (isCDC_Connected's `running`). A masked
// IN_EMPTY with bytes in the ring and a host still there stops the output for good while RX keeps working.
// The rule: bytes wait in the ring, the host is there (SOF), and the ring's free room has not grown for
// SERIAL_WATCH_KICK_MS: kick (unmask IN_EMPTY, flush the FIFO; the ISR then marks the host connected and drains).
// Kicks repeat every SERIAL_WATCH_KICK_MS while the stall lasts; the first room that appears ends it (RESUMED).
// Pure logic: the device glue is src/serial_watch_hw.cpp, the test test/test_serial_watch.
#include <stdbool.h>
#include <stdint.h>

#define SERIAL_WATCH_POLL_MS 250
#define SERIAL_WATCH_KICK_MS 1000

typedef enum { SERIAL_WATCH_NONE, SERIAL_WATCH_KICK, SERIAL_WATCH_RESUMED } serial_watch_act_t;

typedef struct {
  bool started;
  int last_free, max_free; // the ring's free room at the last sample; the most ever seen (= empty ring)
  uint32_t since;          // last progress (room grew or ring empty)
  uint32_t last_kick;
  uint32_t kicks;          // kicks in the current stall
  uint32_t stall_ms;       // on RESUMED: how long the output stood still
  uint32_t resumed_kicks;  // on RESUMED: kicks it took
} serial_watch_t;

void serial_watch_init(serial_watch_t *w);

// One sample: `free` = availableForWrite(), `plugged` = a host sends SOF (usb_serial_jtag_is_connected()).
serial_watch_act_t serial_watch_step(serial_watch_t *w, uint32_t now_ms, int free, bool plugged);
