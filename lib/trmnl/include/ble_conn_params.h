#pragma once

#include <stddef.h>
#include <stdint.h>

// Connection parameters of the Bluetooth LE keyboard link, as pure rules (src/ble_keyboard.cpp carries them out).
// Units as on the air: interval 1.25 ms, supervision timeout 10 ms, latency in connection events.
// A keyboard asks for its own parameters right after connecting (L2CAP signalling or the link layer). Every such
// request gets an answer at once: accepted when the Core spec allows it, refused (0x3B) otherwise. Never left
// waiting: a procedure without an answer times out after 30 s and the keyboard drops the link.

typedef struct {
  uint16_t itvl_min, itvl_max, latency, timeout;
} ble_conn_params_t;

#define BLE_CONN_ANSWER_ACCEPT 0x00
#define BLE_CONN_ANSWER_REJECT 0x3B // unacceptable connection parameters

// The answer to the keyboard's request
uint8_t ble_conn_param_answer(const ble_conn_params_t *req);

// While setting up (security, discovery, subscribing): a fast interval, 7.5 to 15 ms. The keyboard's own request,
// accepted, sets its preferred one after that
ble_conn_params_t ble_conn_setup_params(void);

// One event log line for a request and our answer; returns its length
size_t ble_conn_param_line(char *buf, size_t len, const char *how, const ble_conn_params_t *req, uint8_t answer);
