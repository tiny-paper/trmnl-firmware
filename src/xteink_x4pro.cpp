// Xteink X4 Pro board support: power latch, unused rails, EPD controller detection, CW2017 fuel gauge
// NOT TESTED ON HARDWARE YET - the pin numbers, register reads and sequences come from public
// community projects that report their own hardware tests:
//   Free-Ink/freeink-sdk (MIT), bigbag/papyrix-reader (MIT), clackups/draftling (MIT),
//   crosspoint-reader/crosspoint-reader (MIT)
#ifdef BOARD_XTEINK_X4PRO

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <xteink_x4pro.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "trmnl_log.h"

// The detected controller survives deep sleep; a power-on or reset clears it
RTC_DATA_ATTR static uint8_t u8Controller = X4PRO_CTRL_UNKNOWN;

// Drive a pin and keep it at that level through deep sleep and resets.
// The registers are written first and the hold released after, so a pad that is still held from the
// last deep sleep goes straight to the new level without floating (the power latch must never drop).
static void x4pro_hold_pin(uint8_t pin, uint8_t level)
{
    gpio_set_level((gpio_num_t)pin, level);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, level);
    gpio_hold_dis((gpio_num_t)pin);
    gpio_hold_en((gpio_num_t)pin);
} /* x4pro_hold_pin() */

// The X4 "Classic" uses the same ESP32-S3 module, but has a QMI8658 IMU (WHO_AM_I = 0x05) which the
// Pro lacks, and different EPD and button pins.
static bool x4pro_is_classic(void)
{
    uint8_t u8 = 0;
    Wire.begin(39, 38, 100000);
    Wire.beginTransmission(X4PRO_I2C_ADDR_IMU);
    Wire.write(0x00); // WHO_AM_I
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(X4PRO_I2C_ADDR_IMU, 1) == 1) {
        u8 = Wire.read();
    }
    Wire.end();
    return (u8 == 0x05);
} /* x4pro_is_classic() */

void x4pro_power_init(void)
{
    x4pro_hold_pin(X4PRO_PIN_POWER_HOLD, 1); // before anything else: stay on after the power key is let go
    if (x4pro_is_classic()) {
        // The Classic has buttons and EPD outputs on pins which the Pro drives; leave them all alone
        ets_printf("This is an Xteink X4 Classic, not an X4 Pro - wrong firmware, stopping\n");
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_deep_sleep_start();
    }
    x4pro_hold_pin(X4PRO_PIN_TOUCH_PWR, 1);  // touch rail off
    x4pro_hold_pin(X4PRO_PIN_SD_PWR, 1);     // uSD rail off
    x4pro_hold_pin(X4PRO_PIN_LIGHT_COOL, 0); // frontlight off
    x4pro_hold_pin(X4PRO_PIN_LIGHT_WARM, 0);
} /* x4pro_power_init() */

void x4pro_sleep_prep(void)
{
    x4pro_hold_pin(X4PRO_PIN_POWER_HOLD, 1);
    x4pro_hold_pin(X4PRO_PIN_TOUCH_PWR, 1);
    x4pro_hold_pin(X4PRO_PIN_SD_PWR, 1);
    x4pro_hold_pin(X4PRO_PIN_LIGHT_COOL, 0);
    x4pro_hold_pin(X4PRO_PIN_LIGHT_WARM, 0);
    // The power key has no external pull-up; ext0 keeps the RTC peripherals powered for this one
    rtc_gpio_pullup_en((gpio_num_t)X4PRO_PIN_KEY_POWER);
    rtc_gpio_pulldown_dis((gpio_num_t)X4PRO_PIN_KEY_POWER);
} /* x4pro_sleep_prep() */

//
// EPD controller detection
// The UltraChip parts answer the status (0x71) and revision (0x70) reads on the MOSI line; the SSD1677
// has no such reads and leaves the line alone (it then reads as the pull-up).
//
typedef struct {
    uint8_t sck, mosi, cs, rst, dc;
} X4PRO_SPI;

static void x4pro_spi_write(const X4PRO_SPI *p, uint8_t u8)
{
    for (int i = 0; i < 8; i++) {
        digitalWrite(p->mosi, (u8 & 0x80) ? 1 : 0);
        delayMicroseconds(1);
        digitalWrite(p->sck, 1);
        delayMicroseconds(1);
        digitalWrite(p->sck, 0);
        u8 <<= 1;
    }
} /* x4pro_spi_write() */

// The controller shifts each bit out on the falling clock edge: sample while SCK is low, then clock
static uint8_t x4pro_spi_read(const X4PRO_SPI *p)
{
    uint8_t u8 = 0;
    for (int i = 0; i < 8; i++) {
        delayMicroseconds(1);
        u8 <<= 1;
        u8 |= digitalRead(p->mosi);
        digitalWrite(p->sck, 1);
        delayMicroseconds(1);
        digitalWrite(p->sck, 0);
    }
    return u8;
} /* x4pro_spi_read() */

static void x4pro_cmd_read(const X4PRO_SPI *p, uint8_t u8Cmd, uint8_t *pData, int iLen)
{
    pinMode(p->mosi, OUTPUT);
    digitalWrite(p->dc, 0); // command mode
    digitalWrite(p->cs, 0);
    x4pro_spi_write(p, u8Cmd);
    digitalWrite(p->dc, 1); // data mode
    pinMode(p->mosi, INPUT_PULLUP);
    delayMicroseconds(1);
    for (int i = 0; i < iLen; i++) {
        pData[i] = x4pro_spi_read(p);
    }
    digitalWrite(p->cs, 1);
    pinMode(p->mosi, OUTPUT);
} /* x4pro_cmd_read() */

static void x4pro_probe_pass(const X4PRO_SPI *p, uint8_t *pRev, uint8_t *pFlags, int iResetMs)
{
    digitalWrite(p->rst, 1);
    delay(2);
    digitalWrite(p->rst, 0);
    delay(iResetMs);
    digitalWrite(p->rst, 1);
    delay(30);
    x4pro_cmd_read(p, 0x71, pFlags, 1); // FLG (status)
    x4pro_cmd_read(p, 0x70, pRev, 5);   // REV
} /* x4pro_probe_pass() */

// The factory settings (NVS namespace hw_calib, key screenType) name the controller too.
// It is only used when the probe is not conclusive, since that value was seen to be wrong on some units.
static int x4pro_factory_screen_type(void)
{
    Preferences prefs;
    int iType = -1; // not present
    if (prefs.begin("hw_calib", true)) {
        if (prefs.isKey("screenType")) {
            iType = prefs.getUChar("screenType", 0);
        }
        prefs.end();
    }
    return iType;
} /* x4pro_factory_screen_type() */

int x4pro_detect_controller(uint8_t sck, uint8_t mosi, uint8_t cs, uint8_t rst, uint8_t dc, uint8_t busy)
{
    const X4PRO_SPI spi = {sck, mosi, cs, rst, dc};
    uint8_t u8Rev1[5], u8Rev2[5], u8Flags1, u8Flags2;

    if (u8Controller != X4PRO_CTRL_UNKNOWN) {
        return u8Controller; // known since the last power-on
    }
    pinMode(cs, OUTPUT);
    digitalWrite(cs, 1);
    pinMode(sck, OUTPUT);
    digitalWrite(sck, 0);
    pinMode(dc, OUTPUT);
    pinMode(mosi, OUTPUT);
    pinMode(rst, OUTPUT);
    pinMode(busy, INPUT);
    // Two passes which must agree; the second one uses the longer reset pulse of the UltraChip parts
    x4pro_probe_pass(&spi, u8Rev1, &u8Flags1, 1);
    delay(2);
    x4pro_probe_pass(&spi, u8Rev2, &u8Flags2, x4pro_uc_answer(u8Rev1, u8Flags1) ? 50 : 1);
    Log_info("X4 Pro EPD probe: REV %02x%02x%02x%02x%02x / %02x%02x%02x%02x%02x, FLG %02x / %02x",
             u8Rev1[0], u8Rev1[1], u8Rev1[2], u8Rev1[3], u8Rev1[4], u8Rev2[0], u8Rev2[1], u8Rev2[2], u8Rev2[3],
             u8Rev2[4], u8Flags1, u8Flags2);

    bool bConclusive;
    int iFactory = x4pro_factory_controller(x4pro_factory_screen_type());
    u8Controller = x4pro_classify_controller(u8Rev1, u8Flags1, u8Rev2, u8Flags2, iFactory, &bConclusive);
    if (!bConclusive) {
        Log_error("X4 Pro EPD probe not conclusive, factory setting says %s", x4pro_controller_name(iFactory));
    }
    Log_info("X4 Pro EPD controller: %s", x4pro_controller_name(u8Controller));
    return u8Controller;
} /* x4pro_detect_controller() */

// CW2017 VCELL: 14 bits in registers 2+3 (big-endian), 312.5uV per count
float x4pro_read_battery(uint8_t sda, uint8_t scl)
{
    Wire.begin(sda, scl);
    Wire.beginTransmission(X4PRO_I2C_ADDR_GAUGE);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(X4PRO_I2C_ADDR_GAUGE, 2) != 2) {
        Log_error("CW2017 fuel gauge not answering");
        return -1.0f;
    }
    uint8_t u8High = Wire.read();
    uint8_t u8Low = Wire.read();
    int iMillivolts = x4pro_gauge_millivolts(u8High, u8Low);
    if (iMillivolts < 0) { // the gauge is asleep or it is not a CW2017
        Log_error("CW2017 fuel gauge reading not valid (%02x%02x)", u8High, u8Low);
        return -1.0f;
    }
    return (float)iMillivolts / 1000.0f;
} /* x4pro_read_battery() */

#endif // BOARD_XTEINK_X4PRO
