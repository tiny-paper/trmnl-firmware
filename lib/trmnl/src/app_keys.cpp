#include "app_keys.h"

#include <string.h>

void app_key_decoder_init(app_key_decoder_t *d) {
  d->len = 0;
  d->skip = false;
}

static void key_event(app_event_t *ev, uint16_t key, uint8_t mods, const char *utf8, int utf8_len) {
  memset(ev, 0, sizeof(*ev));
  ev->type = APP_EVENT_KEY_DOWN;
  ev->key = key;
  ev->modifiers = mods;
  if (utf8_len > 0) memcpy(ev->utf8, utf8, (size_t)utf8_len);
}

// The length of a UTF-8 sequence from its first byte, 0 if it cannot start one
static int utf8_len(uint8_t b) {
  if (b < 0x80) return 1;
  if ((b & 0xe0) == 0xc0) return 2;
  if ((b & 0xf0) == 0xe0) return 3;
  if ((b & 0xf8) == 0xf0) return 4;
  return 0;
}

// An escape sequence in d->buf ("\x1b[A", "\x1b[5~", ...): -1 incomplete, 0 unknown, 1 done
static int escape(app_key_decoder_t *d, app_event_t *ev) {
  if (d->len < 2) return -1;
  if (d->buf[1] != '[' && d->buf[1] != 'O') return 0;
  if (d->len < 3) return -1;
  uint8_t last = d->buf[d->len - 1];
  if ((last >= '0' && last <= '9') || last == ';') {
    if (d->len < sizeof(d->buf)) return -1;
    d->skip = true; // too long for any key we know
    return 0;
  }
  uint16_t key = APP_KEY_NONE;
  switch (last) {
  case 'A':
    key = APP_KEY_UP;
    break;
  case 'B':
    key = APP_KEY_DOWN;
    break;
  case 'C':
    key = APP_KEY_RIGHT;
    break;
  case 'D':
    key = APP_KEY_LEFT;
    break;
  case 'H':
    key = APP_KEY_HOME;
    break;
  case 'F':
    key = APP_KEY_END;
    break;
  case 'P':
  case 'Q':
  case 'R':
  case 'S':
    key = (uint16_t)(APP_KEY_F1 + (last - 'P'));
    break;
  case '~':
    if (d->len == 4 && d->buf[2] == '3') key = APP_KEY_DELETE;
    if (d->len == 4 && d->buf[2] == '5') key = APP_KEY_PAGE_UP;
    if (d->len == 4 && d->buf[2] == '6') key = APP_KEY_PAGE_DOWN;
    if (d->len == 4 && (d->buf[2] == '1' || d->buf[2] == '7')) key = APP_KEY_HOME;
    if (d->len == 4 && (d->buf[2] == '4' || d->buf[2] == '8')) key = APP_KEY_END;
    break;
  }
  if (key == APP_KEY_NONE) return 0;
  key_event(ev, key, 0, nullptr, 0);
  return 1;
}

bool app_key_feed(app_key_decoder_t *d, uint8_t byte, app_event_t *ev) {
  if (d->skip) {
    if (byte >= 0x40 && byte <= 0x7e) d->skip = false;
    return false;
  }
  if (d->len == 0) {
    if (byte == 0x1b) {
      d->buf[d->len++] = byte;
      return false;
    }
    if (byte == '\r' || byte == '\n') {
      key_event(ev, APP_KEY_ENTER, 0, nullptr, 0);
      return true;
    }
    if (byte == 0x7f || byte == 0x08) {
      key_event(ev, APP_KEY_BACKSPACE, 0, nullptr, 0);
      return true;
    }
    if (byte == '\t') {
      key_event(ev, APP_KEY_TAB, 0, nullptr, 0);
      return true;
    }
    if (byte >= 0x01 && byte <= 0x1a) { // Ctrl-A .. Ctrl-Z
      char c = (char)('a' + byte - 1);
      key_event(ev, APP_KEY_CHAR, APP_MOD_CTRL, &c, 1);
      return true;
    }
    if (byte < 0x20) return false;
    int n = utf8_len(byte);
    if (n == 0) return false; // a continuation byte without a start
    if (n == 1) {
      char c = (char)byte;
      key_event(ev, APP_KEY_CHAR, 0, &c, 1);
      return true;
    }
    d->buf[d->len++] = byte;
    return false;
  }
  if (d->buf[0] == 0x1b) {
    d->buf[d->len++] = byte;
    int r = escape(d, ev);
    if (r < 0) return false;
    d->len = 0;
    return r == 1;
  }
  // inside a UTF-8 sequence
  if ((byte & 0xc0) != 0x80) {
    d->len = 0;
    return app_key_feed(d, byte, ev); // broken: drop it, start again with this byte
  }
  d->buf[d->len++] = byte;
  if (d->len < utf8_len(d->buf[0])) return false;
  key_event(ev, APP_KEY_CHAR, 0, (const char *)d->buf, d->len);
  d->len = 0;
  return true;
}

bool app_key_idle(app_key_decoder_t *d, app_event_t *ev) {
  bool lone_escape = d->len == 1 && d->buf[0] == 0x1b;
  d->len = 0;
  if (lone_escape) key_event(ev, APP_KEY_ESCAPE, 0, nullptr, 0);
  return lone_escape;
}
