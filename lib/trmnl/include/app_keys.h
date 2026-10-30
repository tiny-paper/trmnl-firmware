#pragma once

#include <stdint.h>

#include <app_api.h>

// Turns the bytes of a serial terminal (UTF-8 text, VT100 escape sequences for the arrows and
// such) into app key events (app_api.h): what a keyboard on the serial port types into an app.

typedef struct {
  uint8_t buf[8];
  uint8_t len;
  bool skip; // dropping the rest of a too long escape sequence, up to its final byte
} app_key_decoder_t;

void app_key_decoder_init(app_key_decoder_t *d);

// Feeds one byte; true when it completes a key, filling *ev with its KEY_DOWN. Bytes that make no
// key (a broken UTF-8 or escape sequence) are dropped.
bool app_key_feed(app_key_decoder_t *d, uint8_t byte, app_event_t *ev);

// No byte came for a while: a lone ESC is the Escape key. True with *ev filled if one was pending.
bool app_key_idle(app_key_decoder_t *d, app_event_t *ev);
