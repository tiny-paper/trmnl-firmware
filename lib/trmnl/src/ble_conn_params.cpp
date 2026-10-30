#include <ble_conn_params.h>
#include <stdio.h>

// Core spec, Vol 6 Part B 4.5.1 and Vol 3 Part A 4.20
#define ITVL_MIN    6 // 7.5 ms
#define ITVL_MAX    3200 // 4 s
#define LATENCY_MAX 499
#define TIMEOUT_MIN 10 // 100 ms
#define TIMEOUT_MAX 3200 // 32 s

uint8_t ble_conn_param_answer(const ble_conn_params_t *req) {
  if (!req) return BLE_CONN_ANSWER_REJECT;
  if (req->itvl_min < ITVL_MIN || req->itvl_max > ITVL_MAX || req->itvl_min > req->itvl_max) return BLE_CONN_ANSWER_REJECT;
  if (req->latency > LATENCY_MAX) return BLE_CONN_ANSWER_REJECT;
  if (req->timeout < TIMEOUT_MIN || req->timeout > TIMEOUT_MAX) return BLE_CONN_ANSWER_REJECT;
  // the timeout must outlast the latency: timeout * 10 ms > (1 + latency) * itvl_max * 1.25 ms * 2
  uint32_t need = (1u + req->latency) * req->itvl_max * 125u * 2u; // in 0.01 ms
  if ((uint32_t)req->timeout * 1000u <= need) return BLE_CONN_ANSWER_REJECT;
  return BLE_CONN_ANSWER_ACCEPT;
}

ble_conn_params_t ble_conn_setup_params(void) { return {6, 12, 0, 400}; } // 7.5 to 15 ms, 4 s timeout

size_t ble_conn_param_line(char *buf, size_t len, const char *how, const ble_conn_params_t *req, uint8_t answer) {
  if (!buf || !len || !req) return 0;
  int n = snprintf(buf, len, "BLE kbd: %s params asked %u-%u (x1.25 ms), latency %u, timeout %u0 ms: %s", how ? how : "?",
                   (unsigned)req->itvl_min, (unsigned)req->itvl_max, (unsigned)req->latency, (unsigned)req->timeout,
                   answer == BLE_CONN_ANSWER_ACCEPT ? "accepted" : "refused");
  if (n < 0) return 0;
  return (size_t)n < len ? (size_t)n : len - 1;
}
