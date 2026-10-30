#include "serial_out_hw.h"

#include <Arduino.h>
#include <serial_out.h>
#include <stdarg.h>
#include <stdio.h>

static int room(void *) { return Serial.availableForWrite(); }
static size_t put(void *, const uint8_t *b, size_t n) { return Serial.write(b, n); }
static uint32_t now(void *) { return millis(); }
static void wait(void *, uint32_t ms) { delay(ms); }
static const serial_out_port_t port = {room, put, now, wait, nullptr};

bool serial_out_printf(const char *fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n < 0) return false;
  size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
  return serial_out_write(&port, (const uint8_t *)line, len, SERIAL_OUT_STALL_MS) == len;
}
