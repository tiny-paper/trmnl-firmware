#pragma once

// What the X keeps of its one bonded keyboard in NVS (Preferences "ble_kbd"): the bond record (name and address;
// NimBLE keeps the keys) and the HID handles (ble_hogp.h hogp_cache_t) that let a reconnect skip discovery.

#include <ble_hogp.h>
#include <ble_pair.h>

// The bond record; nullptr removes it and the handles with it
void kbd_store_bond(const ble_pair_peer_t *peer);
// The kept handles for this keyboard's address; false when none or for another keyboard
bool kbd_store_cache_load(const uint8_t addr[7], hogp_cache_t *out);
void kbd_store_cache_save(const hogp_cache_t *c);
void kbd_store_cache_drop(void);
