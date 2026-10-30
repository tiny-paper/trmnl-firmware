#pragma once
// Paced writes to the X's USB serial port (Arduino HWCDC), for answers that must arrive whole ("TAPP EVLOG").
// HWCDC (cores/esp32/HWCDC.cpp, 3.3.7): write() while it judges the host gone pushes into its TX ring and drops the
// OLDEST bytes when the ring lacks room (flushTXBuffer); flush() discards the whole ring after tx_timeout_ms without
// progress. So: never call flush(), and write only what fits the ring's free space now, waiting for room up to a
// deadline. Pure logic: the port is a table of functions (the device's in src/, a fake in the native test).
#include <stddef.h>
#include <stdint.h>

typedef struct {
  int (*room)(void *ctx);                                // free bytes in the TX ring now (availableForWrite)
  size_t (*write)(void *ctx, const uint8_t *b, size_t n); // bytes taken; n <= room
  uint32_t (*now_ms)(void *ctx);
  void (*wait_ms)(void *ctx, uint32_t ms);
  void *ctx;
} serial_out_port_t;

// Writes `n` bytes; returns how many were taken. Stops early when no room appeared for `stall_ms` in a row (the
// host stopped reading): the rest is not written, nothing already queued is pushed out.
size_t serial_out_write(const serial_out_port_t *p, const uint8_t *b, size_t n, uint32_t stall_ms);

// HWCDC's TX ring on the X (default 256): a burst of log lines or an event log answer waits in it, not in write()
#define SERIAL_TX_RING_BYTES 4096

// The paced answer's stall time: a host that took nothing for this long is not reading
#define SERIAL_OUT_STALL_MS 500
