// The keyboards an app can ask for (keyboards.h): USB (usb_keyboard.cpp); Bluetooth LE not yet
#ifdef BOARD_TRMNL_X

#include <app_api.h>

#include "keyboards.h"

int usb_keyboard_enable(bool on);
bool usb_keyboard_connected(void);
void usb_keyboard_end(void);

int keyboards_enable(uint32_t sources) {
  if (sources & ~(uint32_t)(APP_KBD_USB | APP_KBD_BLE)) return APP_EINVAL;
  if (sources & APP_KBD_BLE) return APP_ENOTSUP;
  return usb_keyboard_enable(sources & APP_KBD_USB);
}

uint32_t keyboards_status(void) { return usb_keyboard_connected() ? APP_KBD_USB : 0; }

void keyboards_end(void) { usb_keyboard_end(); }

#endif // BOARD_TRMNL_X
