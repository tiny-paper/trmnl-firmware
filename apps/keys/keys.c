// Keys: the keyboard test app (docs/apps.md "Keyboards"). Asks for a USB and a Bluetooth keyboard, shows each key with its HID
// usage and modifiers and each keyboard that comes or goes, and returns on a long press.

#include <app_api.h>

#include "common/draw.h"

APP_INFO("Keys", 0);

#define SCALE 6
#define LINE_H (9 * SCALE)
#define X0 120
#define Y0 360
#define LINES 12

static int append(char *buf, int pos, const char *s) {
  while (*s && pos < 63) buf[pos++] = *s++;
  buf[pos] = '\0';
  return pos;
}

static int append_hex(char *buf, int pos, unsigned v, int digits) {
  static const char hex[] = "0123456789abcdef";
  for (int i = digits - 1; i >= 0 && pos < 63; i--) buf[pos++] = hex[(v >> (4 * i)) & 15];
  buf[pos] = '\0';
  return pos;
}

static void describe(const app_event_t *ev, char *line) {
  int p = 0;
  line[0] = '\0';
  if (ev->type == APP_EVENT_KEYBOARD) {
    p = append(line, p, "keyboards now:");
    if (ev->key & APP_KBD_USB) p = append(line, p, " usb");
    if (ev->key & APP_KBD_BLE) p = append(line, p, " ble");
    if (!ev->key) append(line, p, " none");
    return;
  }
  p = append(line, p, ev->type == APP_EVENT_KEY_DOWN ? "down" : "up");
  p = append(line, p, " usage 0x");
  p = append_hex(line, p, ev->hid_usage, 2);
  p = append(line, p, " code 0x");
  p = append_hex(line, p, ev->key, 3);
  p = append(line, p, " mods 0x");
  p = append_hex(line, p, ev->modifiers, 2);
  if (ev->utf8[0]) {
    p = append(line, p, " '");
    p = append(line, p, ev->utf8);
    append(line, p, "'");
  }
}

int app_main(const app_api_t *api) {
  if (api->version < 2 || api->size < sizeof(app_api_t)) return 2;
  draw_t d;
  if (!draw_begin(&d, api)) return 3;
  draw_text(&d, X0, 120, "Keys: a USB keyboard (OTG adapter) or a Bluetooth one", SCALE);
  draw_text(&d, X0, 200, "Unplug the charger: the port is a host only on battery", SCALE);
  draw_text(&d, X0, d.h - 120, "Long press the touch bar to go back", SCALE);
  api->screen_refresh(0, APP_REFRESH_FULL); // drawn before a pairing screen: it comes back after it
  int rc = api->keyboard_enable(APP_KBD_USB | APP_KBD_BLE);
  api->log("keys: keyboard enable %d", rc);
  if (rc) {
    draw_text(&d, X0, 280, "Keyboards refused", SCALE);
    api->screen_refresh(0, APP_REFRESH_FULL);
  }

  char line[64];
  int next = 0;
  app_event_t ev;
  for (;;) {
    if (!api->input_poll(&ev, 1000)) continue;
    if (ev.type == APP_EVENT_EXIT) {
      api->log("keys: exit requested");
      return 0;
    }
    if (ev.type == APP_EVENT_TOUCH) continue;
    describe(&ev, line);
    api->log("keys: %s", line);
    if (next == LINES) {
      draw_fill(&d, X0, Y0, d.w - 2 * X0, LINES * LINE_H, false);
      next = 0;
    }
    int y = Y0 + next * LINE_H;
    draw_text(&d, X0, y, line, SCALE);
    app_rect_t r = {(int16_t)X0, (int16_t)y, (int16_t)(d.w - 2 * X0), (int16_t)LINE_H};
    api->screen_refresh(&r, APP_REFRESH_PARTIAL);
    next++;
  }
}
