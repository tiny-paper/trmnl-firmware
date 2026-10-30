#include "app_serial.h"

#include <string.h>

enum { S_LINE, S_KEYS, S_ESC, S_HEX1, S_HEX2 };

static const char PREFIX[] = "TKEY ";
#define PREFIX_LEN (sizeof(PREFIX) - 1)

void app_serial_init(app_serial_t *s) { memset(s, 0, sizeof(*s)); }

static int hexval(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

app_serial_out_t app_serial_feed(app_serial_t *s, uint8_t b, uint8_t *key, const char **line) {
  bool end = b == '\n' || b == '\r';
  switch (s->state) {
  case S_LINE:
    if (end) {
      s->line[s->len] = '\0';
      bool any = s->len > 0;
      s->len = 0;
      if (!any) return APP_SERIAL_NONE;
      *line = s->line;
      return APP_SERIAL_LINE;
    }
    if (s->len + 1 >= APP_SERIAL_LINE_MAX) { // keep the last bytes
      memmove(s->line, s->line + 1, s->len - 1);
      s->len--;
    }
    s->line[s->len++] = (char)b;
    if (s->len == PREFIX_LEN && memcmp(s->line, PREFIX, PREFIX_LEN) == 0) {
      s->len = 0;
      s->state = S_KEYS;
    }
    return APP_SERIAL_NONE;
  case S_KEYS:
    if (end) {
      s->state = S_LINE;
      return APP_SERIAL_NONE;
    }
    if (b == '\\') {
      s->state = S_ESC;
      return APP_SERIAL_NONE;
    }
    *key = b;
    return APP_SERIAL_KEY;
  case S_ESC: {
    s->state = S_KEYS;
    if (end) {
      s->state = S_LINE;
      return APP_SERIAL_NONE;
    }
    const char *from = "nrte\\", *to = "\n\r\t\x1b\\";
    const char *at = strchr(from, (char)b);
    if (b == 'x') {
      s->state = S_HEX1;
      return APP_SERIAL_NONE;
    }
    if (!at || !b) return APP_SERIAL_NONE; // an unknown escape types nothing
    *key = (uint8_t)to[at - from];
    return APP_SERIAL_KEY;
  }
  case S_HEX1:
  case S_HEX2: {
    int v = hexval(b);
    if (end || v < 0) { // a broken \x: nothing typed
      s->state = end ? S_LINE : S_KEYS;
      return APP_SERIAL_NONE;
    }
    if (s->state == S_HEX1) {
      s->hex = (uint8_t)v;
      s->state = S_HEX2;
      return APP_SERIAL_NONE;
    }
    s->state = S_KEYS;
    *key = (uint8_t)(s->hex << 4 | v);
    return APP_SERIAL_KEY;
  }
  }
  s->state = S_LINE;
  return APP_SERIAL_NONE;
}
