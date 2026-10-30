#pragma once

// The S3's one USB PHY: normally the USB-Serial-JTAG (serial log, flashing over the cable); the USB
// drive moves it to the OTG controller and back. Serial comes back on usb_port_to_serial, and at
// every start (a constructor runs before setup), so a restart, panic or watchdog reset during the
// drive cannot leave it on OTG. ROM flashing with the BOOT button always works: a chip reset
// selects the USB-Serial-JTAG again.

#include <stdbool.h>

// Gives the PHY to the OTG controller (device mode); false if the PHY driver refused
bool usb_port_to_otg(void);

// Back to the USB-Serial-JTAG; the computer sees a new device and enumerates the serial port
void usb_port_to_serial(void);

// True while the OTG controller has the PHY
bool usb_port_on_otg(void);
