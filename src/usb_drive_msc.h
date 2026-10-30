#pragma once

// The USB drive's TinyUSB side (usb_drive.cpp drives it): one mass storage LUN over a RAM image.
// The computer reads and writes only that image; callbacks run in TinyUSB's task.

#include <drive_session.h>
#include <stdint.h>

// Starts TinyUSB on the OTG port (usb_port_to_otg first) serving `sectors` 512-byte sectors of img
bool usb_drive_msc_start(uint8_t *img, uint32_t sectors, drive_session_t *session);

// Stops TinyUSB and its task; after it returns no callback touches the image or the session
void usb_drive_msc_stop(void);

bool usb_drive_msc_mounted(void); // configured by a computer
bool usb_drive_msc_ejected(void); // the computer sent an eject (START STOP UNIT, LoEj)
uint32_t usb_drive_msc_suspended_ms(void); // how long the bus is suspended, 0 if not
