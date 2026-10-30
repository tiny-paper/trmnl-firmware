// A USB keyboard for apps on the TRMNL X: the X as USB host (ESP-IDF usb + espressif/usb_host_hid, boot protocol),
// powered through the charger's 5 V boost (IOX pin 1, OTG). When the port may be a host is usb_port.h's call;
// this file applies it. Device mode (charging, the USB serial port, flashing) is the default and comes back on
// app exit, input power, keyboard removal, a restart or deep sleep.
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <epd_driver.h>
#include <esp_system.h>
#include <hid_keys.h>
#include <trmnl_log.h>
#include <usb/hid_host.h>
#include <usb/usb_host.h>

#include "app_host.h"
#include "keyboards.h"
#include "usb_port_role.h"

#define IOX_CHG_PG       0 // input, low: the charger has input power (VBUS from a charger or computer)
#define IOX_OTG_EN       1 // output, high: the charger's boost drives VBUS
#define STOP_DEADLINE_MS 1000 // the USB stack must be down by then, else a restart

static TaskHandle_t port_task;
static volatile bool wanted, stop_task;
static volatile bool keyboard_open;
static QueueHandle_t conn_q, report_q;
static hid_host_device_handle_t open_dev;
static hid_keys_t keys;

typedef struct {
  uint8_t len;
  uint8_t data[HID_KEYS_REPORT];
  bool gone;
} kbd_report_t;

// ---- the expander, shared with the panel driver ----

static bool expander_lock(uint32_t ms) { return !app_host.panel || xSemaphoreTake(app_host.panel, pdMS_TO_TICKS(ms)); }
static void expander_unlock(void) {
  if (app_host.panel) xSemaphoreGive(app_host.panel);
}

static usb_power_t read_power(void) {
  static usb_power_reading_t reading = {USB_POWER_UNKNOWN, 0};
  bool got = expander_lock(0), input_power = false;
  if (got) {
    bbep.ioPinMode(IOX_CHG_PG, INPUT);
    input_power = bbep.ioRead(IOX_CHG_PG) == 0;
    expander_unlock();
  }
  return usb_power_update(&reading, millis(), got, input_power);
}

static bool boost(bool on, uint32_t wait_ms) {
  if (!expander_lock(wait_ms)) return false;
  bbep.ioPinMode(IOX_OTG_EN, OUTPUT);
  bbep.ioWrite(IOX_OTG_EN, on ? HIGH : LOW);
  expander_unlock();
  return true;
}

// ---- USB host: install, run, stop ----

static void iface_cb(hid_host_device_handle_t h, const hid_host_interface_event_t ev, void *arg) {
  (void)arg;
  kbd_report_t r = {};
  switch (ev) {
  case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
    uint8_t d[16];
    size_t n = 0;
    if (hid_host_device_get_raw_input_report_data(h, d, sizeof(d), &n) != ESP_OK) break;
    r.len = (uint8_t)(n < sizeof(r.data) ? n : sizeof(r.data));
    memcpy(r.data, d, r.len);
    xQueueSend(report_q, &r, 0); // a full queue drops it; never wait in the driver's task
    break;
  }
  case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
    hid_host_device_close(h);
    if (open_dev == h) open_dev = nullptr;
    keyboard_open = false;
    r.gone = true;
    xQueueSend(report_q, &r, 0);
    break;
  default:
    break;
  }
}

static void driver_cb(hid_host_device_handle_t h, const hid_host_driver_event_t ev, void *arg) {
  (void)arg;
  if (ev == HID_HOST_DRIVER_EVENT_CONNECTED) xQueueSend(conn_q, &h, 0);
}

static volatile TaskHandle_t lib_task;
static volatile bool lib_stop;

// The USB host library's events; on lib_stop frees the devices, uninstalls the library and ends
static void lib_loop(void *arg) {
  (void)arg;
  bool freeing = false;
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(50), &flags);
    if (freeing && (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) break;
    if (lib_stop && !freeing) {
      if (usb_host_device_free_all() == ESP_OK) break; // none left to free
      freeing = true; // ESP_ERR_NOT_FINISHED: wait for ALL_FREE
    }
  }
  usb_host_uninstall();
  lib_task = nullptr;
  vTaskDelete(nullptr);
}

static bool host_start(void) {
  if (!usb_port_take(USB_ROLE_HOST)) return false; // the PHY: usb_port_role.cpp, not the host library
  const usb_host_config_t hc = {.skip_phy_setup = true, .intr_flags = ESP_INTR_FLAG_LEVEL1};
  esp_err_t rc = usb_host_install(&hc);
  if (rc != ESP_OK) {
    Log_error("USB keys: host install failed: %s", esp_err_to_name(rc));
    usb_port_give(USB_ROLE_HOST);
    return false;
  }
  lib_stop = false;
  if (xTaskCreatePinnedToCore(lib_loop, "usb_lib", 4096, nullptr, 10, (TaskHandle_t *)&lib_task, 0) != pdPASS) {
    usb_host_uninstall();
    usb_port_give(USB_ROLE_HOST);
    return false;
  }
  hid_host_driver_config_t dc = {};
  dc.create_background_task = true;
  dc.task_priority = 5;
  dc.stack_size = 4096;
  dc.core_id = 0;
  dc.callback = driver_cb;
  rc = hid_host_install(&dc);
  if (rc != ESP_OK) Log_error("USB keys: HID driver install failed: %s", esp_err_to_name(rc));
  return true;
}

// Stops the USB stack; false if it did not stop by `deadline`
static bool host_stop(uint32_t deadline) {
  if (open_dev) {
    hid_host_device_close(open_dev);
    open_dev = nullptr;
  }
  keyboard_open = false;
  while (hid_host_uninstall() != ESP_OK) { // a device still listed: its removal (boost off) takes a moment
    if ((int32_t)(millis() - deadline) >= 0) return false;
    delay(20);
  }
  lib_stop = true;
  while (lib_task) {
    if ((int32_t)(millis() - deadline) >= 0) return false;
    delay(10);
  }
  usb_port_give(USB_ROLE_HOST);
  return true;
}

// A connected HID interface: opened if it is a boot keyboard and none is open yet
static void open_keyboard(hid_host_device_handle_t h) {
  hid_host_dev_params_t p;
  if (hid_host_device_get_params(h, &p) != ESP_OK) return;
  if (p.sub_class != HID_SUBCLASS_BOOT_INTERFACE || p.proto != HID_PROTOCOL_KEYBOARD || open_dev) return;
  const hid_host_device_config_t dc = {.callback = iface_cb, .callback_arg = nullptr};
  if (hid_host_device_open(h, &dc) != ESP_OK) return;
  open_dev = h;
  hid_class_request_set_protocol(h, HID_REPORT_PROTOCOL_BOOT); // many start in it already: not fatal
  hid_class_request_set_idle(h, 0, 0);
  if (hid_host_device_start(h) != ESP_OK) {
    hid_host_device_close(h);
    open_dev = nullptr;
    return;
  }
  hid_keys_init(&keys);
  keyboard_open = true;
  Log_info("USB keys: keyboard connected");
}

static void drain_reports(void) {
  kbd_report_t r;
  app_event_t ev[HID_KEYS_MAX_EVENTS];
  while (xQueueReceive(report_q, &r, 0) == pdTRUE) {
    if (r.gone) {
      keyboards_post(ev, hid_keys_release_all(&keys, ev)); // no key stays down
      Log_info("USB keys: keyboard removed");
      keyboards_post_change();
      continue;
    }
    keyboards_post(ev, hid_keys_report(&keys, r.data, r.len, ev));
  }
}

// ---- the port task ----

static void port_loop(void *arg) {
  (void)arg;
  usb_port_t port;
  usb_port_init(&port);
  usb_port_mode_t applied = USB_PORT_DEVICE;
  usb_port_why_t why = port.why;
  bool was_open = false;
  while (true) {
    hid_host_device_handle_t h;
    if (xQueueReceive(conn_q, &h, pdMS_TO_TICKS(USB_PORT_POLL_MS)) == pdTRUE && applied == USB_PORT_HOST) {
      open_keyboard(h);
    }
    drain_reports();
    if (keyboard_open != was_open) {
      was_open = keyboard_open;
      if (was_open) keyboards_post_change();
    }
    usb_port_input_t in = {wanted && !stop_task, read_power(), keyboard_open, usb_port_role() == USB_ROLE_DRIVE};
    usb_port_mode_t mode = usb_port_step(&port, millis(), &in);
    if (port.why != why) {
      why = port.why;
      Log_info("USB keys: %s", usb_port_why_str(why));
    }
    if (mode == USB_PORT_HOST && applied == USB_PORT_DEVICE) {
      if (host_start()) {
        applied = USB_PORT_HOST;
        if (!boost(true, 200)) Log_error("USB keys: boost not switched on (expander busy)");
      }
    } else if (mode == USB_PORT_DEVICE && applied == USB_PORT_HOST) {
      uint32_t deadline = millis() + STOP_DEADLINE_MS;
      if (!boost(false, STOP_DEADLINE_MS / 2)) {
        Log_error("USB keys: boost could not be switched off - restarting");
        esp_restart(); // the restart's handler and the boot switch it off
      }
      if (!host_stop(deadline)) {
        Log_error("USB keys: USB host did not stop within %d ms - restarting", STOP_DEADLINE_MS);
        esp_restart();
      }
      drain_reports();
      if (was_open) {
        app_event_t ev[HID_KEYS_MAX_EVENTS];
        keyboards_post(ev, hid_keys_release_all(&keys, ev));
        was_open = false;
        keyboards_post_change();
      }
      applied = USB_PORT_DEVICE;
    }
    if (stop_task && applied == USB_PORT_DEVICE) break;
  }
  port_task = nullptr;
  vTaskDelete(nullptr);
}

int usb_keyboard_enable(bool on) {
  // A restart: usb_port_role.cpp's handler gives the PHY back (no I2C there: the boot switches the boost off)
  wanted = on;
  if (!on || port_task) return 0;
  if (!conn_q) conn_q = xQueueCreate(4, sizeof(hid_host_device_handle_t));
  if (!report_q) report_q = xQueueCreate(16, sizeof(kbd_report_t));
  if (!conn_q || !report_q) return APP_ENOMEM;
  stop_task = false;
  if (xTaskCreatePinnedToCore(port_loop, "usb_port", 4096, nullptr, 3, &port_task, 0) != pdPASS) return APP_ENOMEM;
  return 0;
}

bool usb_keyboard_connected(void) { return keyboard_open; }

void usb_keyboard_end(void) {
  wanted = false;
  stop_task = true;
  uint32_t start = millis();
  while (port_task && millis() - start < 3 * STOP_DEADLINE_MS) delay(10);
  if (port_task) {
    Log_error("USB keys: port task did not end - restarting");
    esp_restart();
  }
}

#endif // BOARD_TRMNL_X
