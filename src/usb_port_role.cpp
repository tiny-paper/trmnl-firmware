// The PHY's owner (usb_port_role.h): IDF's usb_phy driver for the OTG roles, the S3's PHY mux in RTC_CNTL usb_conf
// (sw_hw_usb_phy_sel, sw_usb_phy_sel, via usb_serial_jtag_ll_phy_enable_external) for the way back
#ifdef BOARD_TRMNL_X

#include "usb_port_role.h"

#include <esp_private/usb_phy.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hal/usb_serial_jtag_ll.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc_cntl_struct.h>
#include <trmnl_log.h>

static usb_roles_t roles = {USB_ROLE_SERIAL};
static usb_phy_handle_t phy;
static SemaphoreHandle_t lock;

void usb_port_serial_now(void) {
  // The RTC register survives a software reset and deep sleep: the OTG controller keeps the PHY unless given back
  usb_serial_jtag_ll_phy_enable_external(false); // internal PHY -> USB serial/JTAG
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 1;
  USB_SERIAL_JTAG.conf0.dp_pullup = 1;
  REG_CLR_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT); // never into ROM download mode by a reset
  roles.role = USB_ROLE_SERIAL;
}

// Before setup(), after any reset: a reset during the drive or host mode left the mux on OTG
__attribute__((constructor)) static void serial_at_start(void) {
  if (RTCCNTL.usb_conf.sw_usb_phy_sel) usb_port_serial_now();
}

static void IRAM_ATTR before_restart(void) { usb_port_serial_now(); }

static bool locked(void) {
  if (!lock) lock = xSemaphoreCreateMutex();
  return lock && xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE;
}

bool usb_port_take(usb_role_t role) {
  static bool handler;
  if (!handler) handler = esp_register_shutdown_handler(before_restart) == ESP_OK;
  if (!locked()) return false;
  bool ok = false;
  if (roles.role == role && phy) {
    ok = true;
  } else if (usb_role_take(&roles, role)) {
    usb_phy_config_t config = {};
    config.controller = USB_PHY_CTRL_OTG;
    config.target = USB_PHY_TARGET_INT;
    config.otg_mode = role == USB_ROLE_HOST ? USB_OTG_MODE_HOST : USB_OTG_MODE_DEVICE;
    // the host's speed comes from the device it finds (as usb_host_install sets it)
    config.otg_speed = role == USB_ROLE_HOST ? USB_PHY_SPEED_UNDEFINED : USB_PHY_SPEED_FULL;
    esp_err_t err = usb_new_phy(&config, &phy);
    if (err == ESP_OK) {
      ok = true;
      Log_info("USB port: %s", usb_role_str(role));
    } else {
      Log_error("USB port: PHY to %s failed (%s)", usb_role_str(role), esp_err_to_name(err));
      phy = nullptr;
      usb_port_serial_now();
    }
  } else {
    Log_info("USB port: %s refused, %s has it", usb_role_str(role), usb_role_str(roles.role));
  }
  xSemaphoreGive(lock);
  return ok;
}

void usb_port_give(usb_role_t role) {
  if (!locked()) return;
  if (usb_role_give(&roles, role)) {
    if (phy) usb_del_phy(phy);
    phy = nullptr;
    // The pull-up off a moment: the computer sees the OTG device leave, then the serial port arrive
    usb_port_serial_now();
    USB_SERIAL_JTAG.conf0.dp_pullup = 0;
    vTaskDelay(pdMS_TO_TICKS(20));
    USB_SERIAL_JTAG.conf0.dp_pullup = 1;
    Log_info("USB port: serial");
  }
  xSemaphoreGive(lock);
}

usb_role_t usb_port_role(void) { return roles.role; }

#endif // BOARD_TRMNL_X
