// The PHY switch of usb_port.h, with IDF's usb_phy driver and the S3's register for the PHY mux
// (RTC_CNTL usb_conf: sw_hw_usb_phy_sel, sw_usb_phy_sel), as Arduino's usb_switch_to_cdc_jtag does
#ifdef BOARD_TRMNL_X

#include "usb_port.h"

#include <esp_private/usb_phy.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/usb_serial_jtag_ll.h>
#include <soc/rtc_cntl_struct.h>
#include <trmnl_log.h>

static usb_phy_handle_t phy;

static void select_serial_jtag(void) {
  // Internal PHY to the USB-Serial-JTAG (sw_usb_phy_sel 0) and its pads on
  usb_serial_jtag_ll_phy_enable_external(false);
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 1;
  USB_SERIAL_JTAG.conf0.dp_pullup = 1;
}

// Runs before setup(), after any reset: the RTC register survives a software restart
__attribute__((constructor)) static void usb_port_serial_at_start(void) {
  if (RTCCNTL.usb_conf.sw_hw_usb_phy_sel && RTCCNTL.usb_conf.sw_usb_phy_sel) select_serial_jtag();
}

static void IRAM_ATTR before_restart(void) {
  if (RTCCNTL.usb_conf.sw_usb_phy_sel) select_serial_jtag();
}

bool usb_port_to_otg(void) {
  if (phy) return true;
  static bool handler;
  if (!handler) handler = esp_register_shutdown_handler(before_restart) == ESP_OK;
  usb_phy_config_t config = {};
  config.controller = USB_PHY_CTRL_OTG;
  config.target = USB_PHY_TARGET_INT;
  config.otg_mode = USB_OTG_MODE_DEVICE;
  config.otg_speed = USB_PHY_SPEED_FULL;
  esp_err_t err = usb_new_phy(&config, &phy);
  if (err != ESP_OK) {
    Log_error("USB drive: PHY to OTG failed (%s)", esp_err_to_name(err));
    phy = nullptr;
    select_serial_jtag();
    return false;
  }
  return true;
}

void usb_port_to_serial(void) {
  if (phy) {
    usb_del_phy(phy);
    phy = nullptr;
  }
  // The pull-up off a moment: the computer sees the drive leave, then the serial port arrive
  USB_SERIAL_JTAG.conf0.dp_pullup = 0;
  select_serial_jtag();
  USB_SERIAL_JTAG.conf0.dp_pullup = 0;
  vTaskDelay(pdMS_TO_TICKS(20));
  USB_SERIAL_JTAG.conf0.dp_pullup = 1;
}

bool usb_port_on_otg(void) { return phy != nullptr || RTCCNTL.usb_conf.sw_usb_phy_sel; }

#endif // BOARD_TRMNL_X
