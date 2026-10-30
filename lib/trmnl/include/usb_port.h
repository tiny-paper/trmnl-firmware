#pragma once

#include <stdint.h>

// Who has the X's USB-C port (docs/apps.md "Keyboards"): device mode (charging, the serial port, flashing) is the
// default; host mode (the 5 V boost on, a USB keyboard on an OTG adapter) only while an app wants USB keys AND
// the charger has seen no input power for USB_PORT_CLEAR_MS AND a keyboard enumerates. Pure logic: the caller
// polls every USB_PORT_POLL_MS and applies the result (boost and PHY).
//
// Host mode starts as a probe: without a keyboard after USB_PORT_PROBE_MS it is left and tried again after
// USB_PORT_RETRY_MS. It is left in the same step on input power (or an unreadable charger), on the keyboard's
// removal, or when the app no longer wants keys. A restart starts in device mode (usb_port_init).

#define USB_PORT_POLL_MS  100
#define USB_PORT_CLEAR_MS 1000 // no input power this long before the boost may go on
#define USB_PORT_PROBE_MS 3000 // host mode without a keyboard: back to device mode
#define USB_PORT_RETRY_MS 10000 // then this long in device mode before the next probe

typedef enum { USB_POWER_NO, USB_POWER_YES, USB_POWER_UNKNOWN } usb_power_t;

typedef enum { USB_PORT_DEVICE, USB_PORT_HOST } usb_port_mode_t;

typedef enum {
  USB_WHY_NOT_WANTED, // no app wants USB keys
  USB_WHY_INPUT_POWER, // a charger or computer powers the port
  USB_WHY_POWER_UNKNOWN, // the charger could not be read: never boost blind
  USB_WHY_WAIT_CLEAR, // no input power, not yet for USB_PORT_CLEAR_MS
  USB_WHY_PROBING, // host mode, waiting for a keyboard
  USB_WHY_KEYBOARD, // host mode, a keyboard is there
  USB_WHY_NO_KEYBOARD, // the probe found none, or it was removed: waiting USB_PORT_RETRY_MS
} usb_port_why_t;

typedef struct {
  usb_port_mode_t mode;
  usb_port_why_t why;
  bool clear; // no input power since clear_since
  uint32_t clear_since;
  uint32_t host_since; // when host mode began
  uint32_t retry_at; // device mode until then after a probe without a keyboard
  bool retry_wait;
} usb_port_t;

typedef struct {
  bool wanted; // an app asked for USB keys
  usb_power_t power; // the charger's input power good
  bool keyboard; // a HID keyboard is enumerated (host mode only)
} usb_port_input_t;

void usb_port_init(usb_port_t *p);

// One poll at `now_ms`; returns the mode the port must be in now. Host mode means: boost on, PHY to USB-OTG.
usb_port_mode_t usb_port_step(usb_port_t *p, uint32_t now_ms, const usb_port_input_t *in);

const char *usb_port_why_str(usb_port_why_t why);
