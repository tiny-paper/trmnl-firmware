// The bonded keyboard's record and kept HID handles in NVS (ble_keyboard_store.h)
#ifdef BOARD_TRMNL_X

#include <Preferences.h>

#include "ble_keyboard.h"
#include "ble_keyboard_store.h"

#define PREFS_NAMESPACE "ble_kbd"
#define PREFS_PEER      "peer"
#define PREFS_HANDLES   "handles"

bool ble_keyboard_bond(ble_pair_peer_t *out) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, true)) return false;
  bool ok = prefs.getBytesLength(PREFS_PEER) == sizeof(*out) && prefs.getBytes(PREFS_PEER, out, sizeof(*out)) == sizeof(*out);
  prefs.end();
  if (ok) out->name[BLE_PAIR_NAME_MAX - 1] = '\0';
  return ok;
}

void kbd_store_bond(const ble_pair_peer_t *peer) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, false)) return;
  if (peer) {
    prefs.putBytes(PREFS_PEER, peer, sizeof(*peer));
  } else {
    prefs.remove(PREFS_PEER);
    prefs.remove(PREFS_HANDLES);
  }
  prefs.end();
}

bool kbd_store_cache_load(const uint8_t addr[7], hogp_cache_t *out) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, true)) return false;
  bool ok = prefs.getBytesLength(PREFS_HANDLES) == sizeof(*out) && prefs.getBytes(PREFS_HANDLES, out, sizeof(*out)) == sizeof(*out);
  prefs.end();
  return ok && hogp_cache_valid(out, addr);
}

void kbd_store_cache_save(const hogp_cache_t *c) {
  Preferences prefs;
  if (!c || !prefs.begin(PREFS_NAMESPACE, false)) return;
  prefs.putBytes(PREFS_HANDLES, c, sizeof(*c));
  prefs.end();
}

void kbd_store_cache_drop(void) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, false)) return;
  prefs.remove(PREFS_HANDLES);
  prefs.end();
}

#endif // BOARD_TRMNL_X
