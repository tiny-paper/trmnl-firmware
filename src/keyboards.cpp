// The keyboards an app can ask for (keyboards.h): USB (usb_keyboard.cpp), Bluetooth LE (ble_keyboard.cpp)
#ifdef BOARD_TRMNL_X

#include <app_api.h>

#include "app_host.h"
#include "ble_keyboard.h"
#include "keyboards.h"

int usb_keyboard_enable(bool on);
bool usb_keyboard_connected(void);
void usb_keyboard_end(void);

int keyboards_enable(uint32_t sources) {
  if (sources & ~(uint32_t)(APP_KBD_USB | APP_KBD_BLE)) return APP_EINVAL;
  int rc = ble_keyboard_enable(sources & APP_KBD_BLE);
  if (rc) return rc;
  return usb_keyboard_enable(sources & APP_KBD_USB);
}

uint32_t keyboards_status(void) {
  return (usb_keyboard_connected() ? APP_KBD_USB : 0) | (ble_keyboard_connected() ? APP_KBD_BLE : 0);
}

void keyboards_post(const app_event_t *ev, int n) {
  for (int i = 0; i < n; i++) {
    if (!app_host.events) continue;
    if (ev[i].type != APP_EVENT_KEYBOARD) app_host.got_input = true;
    xQueueSend(app_host.events, &ev[i], 0);
  }
}

void keyboards_post_change(void) {
  app_event_t ev = {};
  ev.type = APP_EVENT_KEYBOARD;
  ev.key = (uint16_t)keyboards_status();
  keyboards_post(&ev, 1);
}

void keyboards_end(void) {
  ble_keyboard_end();
  usb_keyboard_end();
}

#endif // BOARD_TRMNL_X
