#pragma once

// Keyboards for apps (app_api.h keyboard_enable, docs/apps.md "Keyboards"): a USB keyboard with the X as USB
// host (usb_keyboard.cpp, the port rules of usb_port.h) and a Bluetooth LE keyboard. Their keys go into the
// running app's event queue.

#include <stdint.h>

// The app asks for keyboards (APP_KBD_* bits; 0 stops them). Returns 0 or APP_ENOTSUP
int keyboards_enable(uint32_t sources);

// APP_KBD_* of the keyboards connected now
uint32_t keyboards_status(void);

// The app ended: every keyboard off, the port back in device mode. Returns once done (at most about 1 s; a USB
// stack that does not stop in time restarts the device, which starts in device mode)
void keyboards_end(void);
