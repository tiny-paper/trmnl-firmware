#include "ble_pair.h"

#include <string.h>

static uint32_t to_list(ble_pair_t *p, const char *message) {
  p->state = BLE_PAIR_LIST;
  p->found_count = 0;
  p->selected = 0;
  p->message = message;
  return BLE_ACT_SCAN | BLE_ACT_SCREEN;
}

static uint32_t connect_bonded(ble_pair_t *p) {
  p->state = BLE_PAIR_CONNECTING;
  return BLE_ACT_CONNECT;
}

uint32_t ble_pair_enable(ble_pair_t *p, const ble_pair_peer_t *bond) {
  memset(p, 0, sizeof(*p));
  if (bond) {
    p->bonded = true;
    p->target = *bond;
    return connect_bonded(p);
  }
  return to_list(p, nullptr);
}

uint32_t ble_pair_disable(ble_pair_t *p) {
  uint32_t act = p->state == BLE_PAIR_CONNECTED ? BLE_ACT_RELEASE | BLE_ACT_DISCONNECT
                 : p->state == BLE_PAIR_CONNECTING || p->state == BLE_PAIR_PASSKEY ? BLE_ACT_DISCONNECT
                                                                                    : 0;
  p->state = BLE_PAIR_OFF;
  return act;
}

uint32_t ble_pair_found(ble_pair_t *p, const ble_pair_peer_t *peer) {
  if (p->state != BLE_PAIR_LIST || !peer) return BLE_ACT_SCAN;
  for (int i = 0; i < p->found_count; i++) {
    if (memcmp(p->found[i].addr, peer->addr, sizeof(peer->addr)) == 0) {
      p->found[i].rssi = peer->rssi;
      if (!p->found[i].name[0] && peer->name[0]) { // a scan response brought the name
        memcpy(p->found[i].name, peer->name, BLE_PAIR_NAME_MAX);
        p->found[i].name[BLE_PAIR_NAME_MAX - 1] = '\0';
        return BLE_ACT_SCAN | BLE_ACT_SCREEN;
      }
      return BLE_ACT_SCAN;
    }
  }
  if (p->found_count == BLE_PAIR_MAX_FOUND) return BLE_ACT_SCAN;
  ble_pair_peer_t *f = &p->found[p->found_count++];
  *f = *peer;
  f->name[BLE_PAIR_NAME_MAX - 1] = '\0';
  return BLE_ACT_SCAN | BLE_ACT_SCREEN;
}

uint32_t ble_pair_touch(ble_pair_t *p, int zone) {
  if (p->state != BLE_PAIR_LIST || p->found_count == 0) return p->state == BLE_PAIR_LIST ? BLE_ACT_SCAN : 0;
  if (zone == 0 || zone == 2) {
    p->selected = (p->selected + (zone == 0 ? p->found_count - 1 : 1)) % p->found_count;
    return BLE_ACT_SCAN | BLE_ACT_SCREEN;
  }
  if (zone != 1) return BLE_ACT_SCAN;
  p->target = p->found[p->selected];
  p->state = BLE_PAIR_CONNECTING;
  p->message = nullptr;
  return BLE_ACT_CONNECT | BLE_ACT_SCREEN; // scanning stops: not set
}

uint32_t ble_pair_passkey(ble_pair_t *p, uint32_t passkey) {
  if (p->state != BLE_PAIR_CONNECTING) return 0;
  p->state = BLE_PAIR_PASSKEY;
  p->passkey = passkey % 1000000;
  return BLE_ACT_SCREEN;
}

uint32_t ble_pair_connected(ble_pair_t *p) {
  if (p->state != BLE_PAIR_CONNECTING && p->state != BLE_PAIR_PASSKEY) return 0;
  bool new_bond = !p->bonded;
  p->bonded = true;
  p->state = BLE_PAIR_CONNECTED;
  // the pairing screen goes away: the app's screen is drawn again by the caller
  return (new_bond ? BLE_ACT_SAVE_BOND | BLE_ACT_SCREEN : 0);
}

uint32_t ble_pair_failed(ble_pair_t *p, ble_fail_t why, uint32_t now) {
  if (p->state != BLE_PAIR_CONNECTING && p->state != BLE_PAIR_PASSKEY) return 0;
  if (p->bonded) {
    if (why == BLE_FAIL_KEY_MISSING || why == BLE_FAIL_AUTH) {
      p->bonded = false;
      return BLE_ACT_DELETE_BOND | to_list(p, "The keyboard forgot this X: pair it again");
    }
    p->state = BLE_PAIR_WAIT;
    p->retry_at = now + BLE_PAIR_RETRY_MS;
    return 0;
  }
  switch (why) {
  case BLE_FAIL_TIMEOUT: return to_list(p, "No answer: put the keyboard in pairing mode");
  case BLE_FAIL_NOT_HID: return to_list(p, "That is not a keyboard");
  default: return BLE_ACT_DELETE_BOND | to_list(p, "Pairing failed: try again");
  }
}

uint32_t ble_pair_disconnected(ble_pair_t *p, uint32_t now) {
  if (p->state == BLE_PAIR_CONNECTED) {
    (void)now;
    p->state = BLE_PAIR_CONNECTING; // at once: a keyboard that wakes advertises only briefly
    return BLE_ACT_RELEASE | BLE_ACT_CONNECT;
  }
  return ble_pair_failed(p, BLE_FAIL_TIMEOUT, now);
}

uint32_t ble_pair_tick(ble_pair_t *p, uint32_t now) {
  if (p->state == BLE_PAIR_WAIT && (int32_t)(now - p->retry_at) >= 0) return connect_bonded(p);
  return p->state == BLE_PAIR_LIST ? BLE_ACT_SCAN : 0;
}
