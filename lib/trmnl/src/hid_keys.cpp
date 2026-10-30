#include "hid_keys.h"

#include <string.h>

void hid_keys_init(hid_keys_t *k) { memset(k, 0, sizeof(*k)); }

uint16_t hid_keys_code(uint8_t usage) {
  switch (usage) {
  case 0x28: return APP_KEY_ENTER;
  case 0x58: return APP_KEY_ENTER; // keypad Enter
  case 0x29: return APP_KEY_ESCAPE;
  case 0x2A: return APP_KEY_BACKSPACE;
  case 0x2B: return APP_KEY_TAB;
  case 0x4C: return APP_KEY_DELETE;
  case 0x4F: return APP_KEY_RIGHT;
  case 0x50: return APP_KEY_LEFT;
  case 0x51: return APP_KEY_DOWN;
  case 0x52: return APP_KEY_UP;
  case 0x4A: return APP_KEY_HOME;
  case 0x4D: return APP_KEY_END;
  case 0x4B: return APP_KEY_PAGE_UP;
  case 0x4E: return APP_KEY_PAGE_DOWN;
  default: break;
  }
  if (usage >= 0x3A && usage <= 0x45) return (uint16_t)(APP_KEY_F1 + (usage - 0x3A));
  return APP_KEY_HID;
}

uint8_t hid_keys_mods(uint8_t m) {
  uint8_t out = 0;
  if (m & 0x22) out |= APP_MOD_SHIFT;
  if (m & 0x11) out |= APP_MOD_CTRL;
  if (m & 0x04) out |= APP_MOD_ALT; // left Alt
  if (m & 0x40) out |= APP_MOD_ALTGR; // right Alt
  if (m & 0x88) out |= APP_MOD_META;
  return out;
}

static bool has(const uint8_t *r, uint8_t usage) {
  for (int i = 2; i < HID_KEYS_REPORT; i++) {
    if (r[i] == usage) return true;
  }
  return false;
}

static void key_event(app_event_t *ev, uint8_t type, uint8_t usage, uint8_t mods) {
  memset(ev, 0, sizeof(*ev));
  ev->type = type;
  ev->modifiers = mods;
  ev->key = hid_keys_code(usage);
  ev->hid_usage = usage;
}

int hid_keys_report(hid_keys_t *k, const uint8_t *report, size_t len, app_event_t out[HID_KEYS_MAX_EVENTS]) {
  if (!report || len < HID_KEYS_REPORT) return 0;
  for (int i = 2; i < HID_KEYS_REPORT; i++) {
    if (report[i] == 0x01) return 0; // ErrorRollOver: the keys are unknown, keep the last state
  }
  uint8_t mods = hid_keys_mods(report[0]);
  int n = 0;
  for (int i = 2; i < HID_KEYS_REPORT; i++) {
    uint8_t u = k->held[i];
    if (u >= 4 && !has(report, u)) key_event(&out[n++], APP_EVENT_KEY_UP, u, mods);
  }
  for (int i = 2; i < HID_KEYS_REPORT; i++) {
    uint8_t u = report[i];
    bool dup = false; // a broken report naming a key twice
    for (int j = 2; j < i; j++) dup = dup || report[j] == u;
    if (u >= 4 && !dup && !has(k->held, u)) key_event(&out[n++], APP_EVENT_KEY_DOWN, u, mods);
  }
  memcpy(k->held, report, HID_KEYS_REPORT);
  return n;
}

int hid_keys_release_all(hid_keys_t *k, app_event_t out[HID_KEYS_MAX_EVENTS]) {
  static const uint8_t none[HID_KEYS_REPORT] = {0};
  int n = hid_keys_report(k, none, sizeof(none), out);
  hid_keys_init(k);
  return n;
}
