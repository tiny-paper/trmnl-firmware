#pragma once

// The one owner of the S3's USB PHY on the TRMNL X (roles in lib/trmnl/include/usb_port.h). The USB serial/JTAG
// (serial log, flashing over the cable) is the default; the keyboard's host mode (usb_keyboard.cpp) and the USB
// drive (usb_drive.cpp) take the PHY for the OTG controller and give it back here. Every way back goes through
// usb_port_serial_now: the boot (wake_init), deep sleep (sleep_session), a restart or panic (shutdown handler),
// the end of an app (runner) and each feature's own end.

#include <usb_port.h>

// The PHY to the OTG controller in `role` (USB_ROLE_HOST or USB_ROLE_DRIVE). False: another role has it, or the
// PHY driver refused (then it is serial again)
bool usb_port_take(usb_role_t role);

// `role` is done: the PHY back to serial if `role` still holds it (with a short D+ drop so the computer
// enumerates the serial port again); nothing when another role holds it
void usb_port_give(usb_role_t role);

// Who has the PHY now
usb_role_t usb_port_role(void);

// The switch-back itself: PHY to the USB serial/JTAG, its pads on, the ROM's forced download boot cleared. No
// wait and no lock, safe in a shutdown handler; the role record becomes serial
void usb_port_serial_now(void);
