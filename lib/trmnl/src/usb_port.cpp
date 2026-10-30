#include "usb_port.h"

#include <string.h>

void usb_roles_init(usb_roles_t *r) { r->role = USB_ROLE_SERIAL; }

bool usb_role_take(usb_roles_t *r, usb_role_t want) {
  if (want == USB_ROLE_SERIAL) return false;
  if (r->role != USB_ROLE_SERIAL && r->role != want) return false;
  r->role = want;
  return true;
}

bool usb_role_give(usb_roles_t *r, usb_role_t had) {
  if (had == USB_ROLE_SERIAL || r->role != had) return false;
  r->role = USB_ROLE_SERIAL;
  return true;
}

const char *usb_role_str(usb_role_t role) {
  switch (role) {
  case USB_ROLE_SERIAL: return "serial";
  case USB_ROLE_HOST: return "USB host";
  case USB_ROLE_DRIVE: return "USB drive";
  }
  return "?";
}

void usb_port_init(usb_port_t *p) {
  memset(p, 0, sizeof(*p));
  p->mode = USB_PORT_DEVICE;
  p->why = USB_WHY_NOT_WANTED;
}

static usb_port_mode_t device(usb_port_t *p, usb_port_why_t why) {
  p->mode = USB_PORT_DEVICE;
  p->why = why;
  return p->mode;
}

usb_port_mode_t usb_port_step(usb_port_t *p, uint32_t now, const usb_port_input_t *in) {
  if (in->power != USB_POWER_NO) {
    p->clear = false;
    p->retry_wait = false; // the next time on battery is a new chance
    return device(p, in->power == USB_POWER_YES ? USB_WHY_INPUT_POWER : USB_WHY_POWER_UNKNOWN);
  }
  if (!p->clear) {
    p->clear = true;
    p->clear_since = now;
  }
  if (!in->wanted) {
    p->retry_wait = false;
    return device(p, USB_WHY_NOT_WANTED);
  }
  if (in->port_taken) return device(p, USB_WHY_PORT_TAKEN); // never pull the PHY from the drive
  if (p->mode == USB_PORT_HOST) {
    if (in->keyboard) {
      p->why = USB_WHY_KEYBOARD;
      return p->mode;
    }
    // a keyboard that was there is gone, or the probe found none
    if (p->why == USB_WHY_KEYBOARD || (uint32_t)(now - p->host_since) >= USB_PORT_PROBE_MS) {
      p->retry_wait = true;
      p->retry_at = now + USB_PORT_RETRY_MS;
      return device(p, USB_WHY_NO_KEYBOARD);
    }
    return p->mode; // probing
  }
  if ((uint32_t)(now - p->clear_since) < USB_PORT_CLEAR_MS) return device(p, USB_WHY_WAIT_CLEAR);
  if (p->retry_wait && (int32_t)(now - p->retry_at) < 0) return device(p, USB_WHY_NO_KEYBOARD);
  p->retry_wait = false;
  p->mode = USB_PORT_HOST;
  p->why = USB_WHY_PROBING;
  p->host_since = now;
  return p->mode;
}

const char *usb_port_why_str(usb_port_why_t why) {
  switch (why) {
  case USB_WHY_NOT_WANTED: return "no app wants USB keys";
  case USB_WHY_INPUT_POWER: return "input power: device mode";
  case USB_WHY_POWER_UNKNOWN: return "charger unreadable: device mode";
  case USB_WHY_WAIT_CLEAR: return "no input power, checking";
  case USB_WHY_PROBING: return "host mode: looking for a keyboard";
  case USB_WHY_KEYBOARD: return "host mode: keyboard";
  case USB_WHY_NO_KEYBOARD: return "no keyboard: device mode, trying again later";
  case USB_WHY_PORT_TAKEN: return "the USB drive has the port";
  }
  return "?";
}
