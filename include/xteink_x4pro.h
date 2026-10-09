#pragma once
// Xteink X4 Pro (ESP32-S3, 16MB flash, 8MB octal PSRAM, 4.3" 800x480 B/W SPI panel)
// Board specific helpers: power latch, unused rails, EPD controller detection, CW2017 fuel gauge
#ifdef BOARD_XTEINK_X4PRO

#include <stdint.h>
#include <x4pro_panel.h> // the controller types and the detection rules

// GPIO connections (the EPD and I2C lines are in device_list[] in display.cpp)
#define X4PRO_PIN_KEY_LEFT   0  // BOOT strap; not used
#define X4PRO_PIN_POWER_HOLD 1  // HIGH keeps the board powered after the power key is released
#define X4PRO_PIN_TOUCH_PWR  2  // GT911 touch rail, LOW = on (not used)
#define X4PRO_PIN_KEY_POWER  3  // the TRMNL button (active LOW, needs the internal pull-up)
#define X4PRO_PIN_SD_PWR     5  // uSD rail, LOW = on (not used)
#define X4PRO_PIN_KEY_RIGHT  7  // not used
#define X4PRO_PIN_LIGHT_COOL 8  // frontlight, HIGH = on (not used)
#define X4PRO_PIN_LIGHT_WARM 9  // frontlight, HIGH = on (not used)

#define X4PRO_I2C_ADDR_GAUGE 0x63 // CW2017
#define X4PRO_I2C_ADDR_IMU   0x6b // QMI8658, only present on the X4 "Classic"

// Call first thing at boot: holds the power on and parks the unused rails.
// If the board turns out to be an X4 Classic (different EPD and button pins) it does not return.
void x4pro_power_init(void);
// Call right before esp_deep_sleep_start()
void x4pro_sleep_prep(void);
// Bit-bang probe of the EPD controller; must run before bb_epaper takes over the SPI pins.
// The answer is kept in RTC memory, so the probe only runs after a power-on or reset.
int x4pro_detect_controller(uint8_t sck, uint8_t mosi, uint8_t cs, uint8_t rst, uint8_t dc, uint8_t busy);
// Battery voltage from the CW2017 (Volts), or a negative value if the gauge gave no valid reading
float x4pro_read_battery(uint8_t sda, uint8_t scl);

#endif // BOARD_XTEINK_X4PRO
