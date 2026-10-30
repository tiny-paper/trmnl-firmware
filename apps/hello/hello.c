// Hello: the example app (docs/apps.md). Draws a title and a box, shows each key and touch bar
// event, counts its runs in a setting, and returns on a long press (APP_EVENT_EXIT).

#include <app_api.h>

#include "../common/draw.h"

APP_INFO("Hello", 0);

#define SCALE 6
#define LINE_H (9 * SCALE)
#define BOX_X 120
#define BOX_Y 360
#define BOX_LINES 8

static int append(char *buf, int pos, const char *s) {
  while (*s && pos < 63) buf[pos++] = *s++;
  buf[pos] = '\0';
  return pos;
}

static int append_int(char *buf, int pos, unsigned v) {
  char tmp[11];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < 10);
  while (n && pos < 63) buf[pos++] = tmp[--n];
  buf[pos] = '\0';
  return pos;
}

static const char *const zones[] = {"left", "middle", "right"};
static const char *const gestures[] = {"tap", "swipe left", "swipe right"};

static void describe(const app_event_t *ev, char *line) {
  int p = 0;
  line[0] = '\0';
  if (ev->type == APP_EVENT_TOUCH) {
    p = append(line, p, "touch ");
    p = append(line, p, ev->zone < 3 ? zones[ev->zone] : "?");
    p = append(line, p, " ");
    append(line, p, ev->gesture < 3 ? gestures[ev->gesture] : "?");
  } else {
    p = append(line, p, ev->type == APP_EVENT_KEY_DOWN ? "key down " : "key up ");
    if (ev->utf8[0]) {
      p = append(line, p, "'");
      p = append(line, p, ev->utf8);
      p = append(line, p, "' ");
    }
    p = append(line, p, "code ");
    p = append_int(line, p, ev->key);
    p = append(line, p, " mods ");
    append_int(line, p, ev->modifiers);
  }
}

int app_main(const app_api_t *api) {
  if (api->version < 1 || api->size < sizeof(app_api_t)) return 2;
  draw_t d;
  if (!draw_begin(&d, api)) return 3;

  uint32_t runs = 0;
  api->setting_get("runs", &runs, sizeof(runs));
  runs++;
  api->setting_set("runs", &runs, sizeof(runs));
  api->log("hello: run %u", (unsigned)runs);

  char line[64];
  draw_text(&d, BOX_X, 120, "Hello from an app", SCALE * 2);
  int p = append(line, 0, "Run ");
  p = append_int(line, p, runs);
  p = append(line, p, ", screen ");
  p = append_int(line, p, (unsigned)d.w);
  p = append(line, p, "x");
  append_int(line, p, (unsigned)d.h);
  draw_text(&d, BOX_X, 240, line, SCALE);
  draw_text(&d, BOX_X, d.h - 120, "Long press the touch bar to go back", SCALE);
  const int box_w = d.w - 2 * BOX_X, box_h = BOX_LINES * LINE_H + 2 * LINE_H;
  draw_frame(&d, BOX_X - 30, BOX_Y - 30, box_w + 60, box_h, 6);
  draw_text(&d, BOX_X, BOX_Y, "Press a key or tap the touch bar", SCALE);
  api->screen_refresh(0, APP_REFRESH_FULL);

  int next_line = 1;
  app_event_t ev;
  for (;;) {
    if (!api->input_poll(&ev, 1000)) continue;
    if (ev.type == APP_EVENT_EXIT) {
      api->log("hello: exit requested");
      return 0;
    }
    if (ev.type == APP_EVENT_KEY_UP) continue;
    describe(&ev, line);
    api->log("hello: %s", line);
    if (next_line > BOX_LINES) {
      draw_fill(&d, BOX_X, BOX_Y + LINE_H, box_w, BOX_LINES * LINE_H, false);
      next_line = 1;
    }
    int y = BOX_Y + next_line * LINE_H;
    draw_fill(&d, BOX_X, y, box_w, LINE_H, false);
    draw_text(&d, BOX_X, y, line, SCALE);
    app_rect_t r = {(int16_t)BOX_X, (int16_t)y, (int16_t)box_w, (int16_t)LINE_H};
    api->screen_refresh(&r, APP_REFRESH_PARTIAL);
    next_line++;
  }
}
