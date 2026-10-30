// The Bluetooth keyboard pairing screen (ble_keyboard.h), drawn by the firmware over a running app: the
// keyboards found (name, signal), the one picked, then the passkey in big digits. The app's pixels are kept
// in PSRAM while it shows and put back afterwards; the app's own refreshes wait (app_host.screen_held).
#ifdef BOARD_TRMNL_X

#include <epd_driver.h>
#include <esp_heap_caps.h>
#include <fonts.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_host.h"
#include "ble_keyboard.h"

static uint8_t *saved; // the app's buffer while the pairing screen is up
static size_t saved_len;
static bool shown_once; // a full refresh first, partial ones after

static size_t buffer_len(void) {
  int w = bbep.width(), h = bbep.height();
  switch (bbep.getMode()) {
  case BB_MODE_2BPP: return (size_t)((w + 3) / 4) * h;
  case BB_MODE_4BPP: return (size_t)((w + 1) / 2) * h;
  default: return (size_t)((w + 7) / 8) * h;
  }
}

static uint8_t white(void) { return bbep.getMode() == BB_MODE_1BPP ? BBEP_WHITE : (bbep.getMode() == BB_MODE_2BPP ? 3 : 0xf); }

// A seven-segment digit, `h` high: readable from a distance, no big font needed
static void draw_digit(int x, int y, int h, int d) {
  static const uint8_t seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // a..g
  int w = h / 2, t = h / 9, half = h / 2;
  const struct {
    int x, y, w, h;
  } r[7] = {
      {x, y, w, t}, // a
      {x + w - t, y, t, half}, // b
      {x + w - t, y + half, t, half}, // c
      {x, y + h - t, w, t}, // d
      {x, y + half, t, half}, // e
      {x, y, t, half}, // f
      {x, y + half - t / 2, w, t}, // g
  };
  for (int i = 0; i < 7; i++) {
    if (seg[d] & (1 << i)) bbep.fillRect(r[i].x, r[i].y, r[i].w, r[i].h, BBEP_BLACK);
  }
}

static void draw(const ble_pair_t *p) {
  int w = bbep.width(), h = bbep.height(), x = w / 8;
  bbep.fillScreen(white());
  bbep.setTextColor(BBEP_BLACK, white());
  bbep.setFont(Roboto_Black_24);
  bbep.drawString("Bluetooth keyboard", x, h / 8);
  bbep.setFont(Inter_18);
  int y = h / 8 + 110;
  if (p->message) {
    bbep.drawString(p->message, x, y);
    y += 80;
  }
  char line[64];
  switch (p->state) {
  case BLE_PAIR_LIST:
    bbep.drawString("Put the keyboard in pairing mode (POP Icon Keys: hold an Easy-Switch key 3 s).", x, y);
    y += 100;
    if (p->found_count == 0) bbep.drawString("Looking for keyboards...", x, y);
    for (int i = 0; i < p->found_count; i++) {
      const ble_pair_peer_t &f = p->found[i];
      snprintf(line, sizeof(line), "%s   %d dBm", f.name[0] ? f.name : "(no name)", f.rssi);
      if (i == p->selected) bbep.drawRect(x - 30, y + i * 80 - 52, w - 2 * x + 60, 72, BBEP_BLACK);
      bbep.drawString(line, x, y + i * 80);
    }
    bbep.drawString("Left / right: choose    Middle: pair    Hold: end the app", x, h - 120);
    break;
  case BLE_PAIR_CONNECTING:
    snprintf(line, sizeof(line), "Connecting to %s...", p->target.name[0] ? p->target.name : "the keyboard");
    bbep.drawString(line, x, y);
    break;
  case BLE_PAIR_PASSKEY: {
    snprintf(line, sizeof(line), "Type this on %s, then Enter:", p->target.name[0] ? p->target.name : "the keyboard");
    bbep.drawString(line, x, y);
    int dh = h / 4, gap = dh / 4, dx = x;
    uint32_t key = p->passkey;
    int digits[6];
    for (int i = 5; i >= 0; i--, key /= 10) digits[i] = (int)(key % 10);
    for (int i = 0; i < 6; i++, dx += dh / 2 + gap) draw_digit(dx, y + 80, dh, digits[i]);
    snprintf(line, sizeof(line), "%06lu", (unsigned long)p->passkey);
    bbep.drawString(line, x, y + 80 + dh + 90); // the same as text
    break;
  }
  default:
    break;
  }
}

void ble_screen_show(const ble_pair_t *p) {
  if (!app_host.panel) return;
  xSemaphoreTake(app_host.panel, portMAX_DELAY);
  if (!app_host.screen_held) {
    saved_len = buffer_len();
    saved = (uint8_t *)heap_caps_malloc(saved_len, MALLOC_CAP_SPIRAM);
    if (saved && bbep.currentBuffer()) memcpy(saved, bbep.currentBuffer(), saved_len);
    else Log_error("BLE keys: no memory to keep the app's screen; it is cleared after pairing");
    app_host.screen_held = true;
    shown_once = false;
  }
  draw(p);
  if (shown_once && bbep.getMode() == BB_MODE_1BPP) bbep.partialUpdate(true);
  else bbep.fullUpdate(CLEAR_FAST, true);
  shown_once = true;
  xSemaphoreGive(app_host.panel);
}

void ble_screen_release(void) {
  if (!app_host.screen_held || !app_host.panel) return;
  xSemaphoreTake(app_host.panel, portMAX_DELAY);
  if (bbep.currentBuffer()) {
    if (saved && buffer_len() == saved_len) memcpy(bbep.currentBuffer(), saved, saved_len);
    else bbep.fillScreen(white());
    bbep.fullUpdate(CLEAR_FAST, true);
  }
  heap_caps_free(saved);
  saved = nullptr;
  app_host.screen_held = false;
  xSemaphoreGive(app_host.panel);
}

#endif // BOARD_TRMNL_X
