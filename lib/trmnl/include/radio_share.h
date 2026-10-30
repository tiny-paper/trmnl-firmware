#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Sharing the S3's one 2.4 GHz radio between Wi-Fi and a Bluetooth LE keyboard, and an app's net_up (docs/apps.md
// "Bluetooth LE keyboard"). Pure rules: src/app_api_table.cpp and src/ble_keyboard.cpp carry them out.

// BLE scan timing (units of 0.625 ms): a window shorter than the interval leaves the rest to Wi-Fi
typedef struct {
  uint16_t itvl, window;
} ble_share_scan_t;

// Listing keyboards (ble_gap_disc)
ble_share_scan_t ble_share_list(void);
// Connecting the picked or bonded keyboard (ble_gap_connect). NimBLE's default is a window as long as the
// interval: the radio all BLE for the whole attempt, every attempt, while a bonded keyboard sleeps
ble_share_scan_t ble_share_connect(void);
// Percent of the radio time a scan takes
unsigned ble_share_duty(ble_share_scan_t s);

typedef enum {
  NET_UP_ALREADY, // the station is connected
  NET_UP_NO_SAVED, // no saved network
  NET_UP_5GHZ, // the saved network is 5 GHz: the modem has no HTTP client for apps
  NET_UP_JOIN, // join the saved 2.4 GHz network
} net_up_step_t;

typedef struct {
  bool connected, saved, is5ghz;
} net_up_state_t;

net_up_step_t net_up_step(net_up_state_t s);

// Wi-Fi modem sleep while joining and after: on while BLE is on (the coexistence scheduler needs it to give each
// its slots), off otherwise (TRMNL's own connect)
bool net_up_modem_sleep(bool ble_on);

typedef struct {
  net_up_step_t step;
  bool up;
  uint32_t ms;
  int wl_status, rssi;
  bool ble_on, kbd_connected;
  uint32_t internal_free;
} net_up_report_t;

// The one log line per net_up call: "Net: up in N ms ..." or "Net: failed: <reason> ..."; returns its length
size_t net_up_log_line(char *buf, size_t len, const net_up_report_t *r);
