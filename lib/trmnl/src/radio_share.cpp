#include <radio_share.h>
#include <stdio.h>

// a 30 ms window every 50 ms
ble_share_scan_t ble_share_list(void) { return {0x50, 0x30}; }
ble_share_scan_t ble_share_connect(void) { return {0x50, 0x30}; }

unsigned ble_share_duty(ble_share_scan_t s) { return s.itvl ? (unsigned)s.window * 100u / s.itvl : 100u; }

net_up_step_t net_up_step(net_up_state_t s) {
  if (s.connected) return NET_UP_ALREADY;
  if (!s.saved) return NET_UP_NO_SAVED;
  if (s.is5ghz) return NET_UP_5GHZ;
  return NET_UP_JOIN;
}

bool net_up_modem_sleep(bool ble_on) { return ble_on; }

static const char *reason(net_up_step_t step) {
  switch (step) {
  case NET_UP_NO_SAVED: return "no saved network";
  case NET_UP_5GHZ: return "saved network is 5 GHz (no app HTTP on the modem)";
  case NET_UP_JOIN: return "join failed";
  case NET_UP_ALREADY: break;
  }
  return "?";
}

size_t net_up_log_line(char *buf, size_t len, const net_up_report_t *r) {
  const char *ble = !r->ble_on ? "ble off" : r->kbd_connected ? "ble on, keyboard connected" : "ble on, no keyboard";
  int n;
  if (r->up) {
    n = snprintf(buf, len, "Net: up in %lu ms (%s, rssi %d, %s)", (unsigned long)r->ms,
                 r->step == NET_UP_ALREADY ? "already" : "joined", r->rssi, ble);
  } else {
    n = snprintf(buf, len, "Net: failed: %s after %lu ms, wl_status %d, rssi %d, %s, internal RAM %lu free",
                 reason(r->step), (unsigned long)r->ms, r->wl_status, r->rssi, ble, (unsigned long)r->internal_free);
  }
  if (n < 0) return 0;
  return (size_t)n < len ? (size_t)n : (len ? len - 1 : 0);
}
