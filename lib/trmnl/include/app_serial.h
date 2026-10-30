#pragma once

// The serial port's lines while an app runs (docs/apps.md "Serial port"), I/O free. Only a "TKEY <text>" line types
// into the app: <text> with \n \r \t \e \\ and \xHH escapes, fed to app_keys.h. Every other line ("TAPP ..." commands,
// boot noise, a terminal's stray bytes) is handed back whole and never reaches the app as keys. A line ends at \r or
// \n; the command part keeps its last APP_SERIAL_LINE_MAX - 1 bytes.

#include <stdint.h>

#define APP_SERIAL_LINE_MAX 32

typedef enum { APP_SERIAL_NONE, APP_SERIAL_KEY, APP_SERIAL_LINE } app_serial_out_t;

typedef struct {
  char line[APP_SERIAL_LINE_MAX];
  uint8_t len;
  uint8_t state; // private
  uint8_t hex;
} app_serial_t;

void app_serial_init(app_serial_t *s);

// One byte: APP_SERIAL_KEY with *key a byte for app_key_feed, APP_SERIAL_LINE with *line a finished non-TKEY line
// (NUL-terminated, valid until the next call), or APP_SERIAL_NONE
app_serial_out_t app_serial_feed(app_serial_t *s, uint8_t b, uint8_t *key, const char **line);
