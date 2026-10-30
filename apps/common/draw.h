// Drawing helpers for apps on the 1-bit screen (app_api.h, APP_SCREEN_1BIT: 1 = white)
#pragma once

#include <app_api.h>

#include "font5x7.h"

typedef struct {
  const app_api_t *api;
  uint8_t *fb;
  int w, h, stride;
} draw_t;

static inline bool draw_begin(draw_t *d, const app_api_t *api) {
  d->api = api;
  d->fb = api->screen_set_mode(APP_SCREEN_1BIT);
  d->w = api->screen_width();
  d->h = api->screen_height();
  d->stride = api->screen_stride();
  return d->fb != 0;
}

// black: true for black, false for white
static inline void draw_fill(draw_t *d, int x, int y, int w, int h, bool black) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > d->w) w = d->w - x;
  if (y + h > d->h) h = d->h - y;
  for (int row = y; row < y + h; row++) {
    uint8_t *line = d->fb + row * d->stride;
    for (int col = x; col < x + w; col++) {
      uint8_t bit = 0x80 >> (col & 7);
      if (black) line[col >> 3] &= ~bit;
      else line[col >> 3] |= bit;
    }
  }
}

static inline void draw_frame(draw_t *d, int x, int y, int w, int h, int t) {
  draw_fill(d, x, y, w, t, true);
  draw_fill(d, x, y + h - t, w, t, true);
  draw_fill(d, x, y, t, h, true);
  draw_fill(d, x + w - t, y, t, h, true);
}

// Draws text with the 5x7 font, each dot `scale` pixels; returns the x after it
static inline int draw_text(draw_t *d, int x, int y, const char *s, int scale) {
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x20 || c > 0x7e) c = '?';
    for (int col = 0; col < 5; col++) {
      uint8_t bits = font5x7[c - 0x20][col];
      for (int row = 0; row < 7; row++) {
        if (bits & (1 << row)) draw_fill(d, x + col * scale, y + row * scale, scale, scale, true);
      }
    }
    x += 6 * scale;
  }
  return x;
}
