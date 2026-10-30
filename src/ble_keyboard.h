#pragma once

// A Bluetooth LE keyboard for apps (docs/apps.md "Keyboards"): NimBLE as central, the pairing rules of
// ble_pair.h, the HID over GATT steps of ble_hogp.h. The radio is on only while an app wants the keyboard.

#include <ble_pair.h>
#include <stdint.h>

// The app asks for (true) or stops (false) the Bluetooth keyboard. Returns 0 or APP_ENOMEM
int ble_keyboard_enable(bool on);
bool ble_keyboard_connected(void);
// The app ended: link down, radio off, the app's screen back. Returns once done
void ble_keyboard_end(void);
// A touch bar tap while the pairing screen is up: true if it was taken (not the app's)
bool ble_keyboard_tap(int zone);
// The radio is on: Wi-Fi shares it (modem sleep, coexistence)
bool ble_keyboard_radio_on(void);

// The bonded keyboard, without the radio (the Apps menu): its name, false if none
bool ble_keyboard_bond(ble_pair_peer_t *out);
// Forget it: our record and NimBLE's keys (NVS namespace nimble_bond)
void ble_keyboard_forget(void);

// The pairing screen (ble_pair_screen.cpp)
void ble_screen_show(const ble_pair_t *p);
void ble_screen_release(void);
