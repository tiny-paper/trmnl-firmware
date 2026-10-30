#pragma once

#include <stdint.h>

// Pairing and reconnecting a Bluetooth LE keyboard for an app (docs/apps.md "Keyboards"). Pure logic: the
// caller feeds radio and touch bar events and carries out the returned actions with NimBLE.
//
// No bond: scan and list the keyboards found; the touch bar picks one (left/right move, middle connects); a
// passkey the keyboard asks for is shown to be typed on it. A bond (kept in NVS by NimBLE's store) connects
// directly and again after every disconnect (sleep, out of range, Easy-Switch to another channel). A bond the
// keyboard no longer has (it was paired elsewhere) is deleted and the list comes back.

#define BLE_PAIR_MAX_FOUND 6
#define BLE_PAIR_NAME_MAX  24
#define BLE_PAIR_RETRY_MS  2000 // after a failed connect to the bonded keyboard

typedef struct {
  uint8_t addr[7]; // type, then 6 bytes
  char name[BLE_PAIR_NAME_MAX];
  int8_t rssi;
} ble_pair_peer_t;

typedef enum {
  BLE_PAIR_OFF,
  BLE_PAIR_LIST, // scanning, the list on screen
  BLE_PAIR_CONNECTING,
  BLE_PAIR_PASSKEY, // the passkey on screen
  BLE_PAIR_CONNECTED,
  BLE_PAIR_WAIT, // bonded keyboard away: connect again at retry_at
} ble_pair_state_t;

typedef enum {
  BLE_FAIL_TIMEOUT, // nothing answered (keyboard asleep, away)
  BLE_FAIL_KEY_MISSING, // encryption refused: the keyboard lost our bond
  BLE_FAIL_AUTH, // pairing refused or a wrong passkey
  BLE_FAIL_NOT_HID, // connected, but no keyboard service
} ble_fail_t;

// Actions for the caller, as bits
#define BLE_ACT_SCAN        0x01 // start scanning (stop when not set and scanning)
#define BLE_ACT_CONNECT     0x02 // connect to `target`
#define BLE_ACT_SAVE_BOND   0x04 // keep `target`'s bond (the only one: older ones are deleted)
#define BLE_ACT_DELETE_BOND 0x08 // delete the bond with `target`
#define BLE_ACT_SCREEN      0x10 // draw the pairing screen again (list, selection, passkey or message)
#define BLE_ACT_RELEASE     0x20 // the keyboard is gone: release held keys, tell the app
#define BLE_ACT_DISCONNECT  0x40 // drop the link (disable)

typedef struct {
  ble_pair_state_t state;
  bool bonded;
  ble_pair_peer_t target; // the bonded or picked keyboard
  ble_pair_peer_t found[BLE_PAIR_MAX_FOUND];
  int found_count;
  int selected;
  uint32_t passkey;
  uint32_t retry_at;
  const char *message; // shown on the list screen, e.g. why the last try failed; nullptr for none
} ble_pair_t;

// Enabled by the app; `bond` is the stored bond or nullptr
uint32_t ble_pair_enable(ble_pair_t *p, const ble_pair_peer_t *bond);
uint32_t ble_pair_disable(ble_pair_t *p);

// A device seen while scanning; only keyboards (appearance or HID service) should be passed
uint32_t ble_pair_found(ble_pair_t *p, const ble_pair_peer_t *peer);

// The touch bar on the list screen: 0 left, 1 middle, 2 right
uint32_t ble_pair_touch(ble_pair_t *p, int zone);

uint32_t ble_pair_passkey(ble_pair_t *p, uint32_t passkey);
uint32_t ble_pair_connected(ble_pair_t *p); // encrypted and the HID reports subscribed
uint32_t ble_pair_failed(ble_pair_t *p, ble_fail_t why, uint32_t now_ms);
uint32_t ble_pair_disconnected(ble_pair_t *p, uint32_t now_ms);
uint32_t ble_pair_tick(ble_pair_t *p, uint32_t now_ms);
