#include <espnow_rx.h>
#include <string.h>

int radio_claim_check(radio_users_t now, radio_want_t want) {
  if (now.espnow) return APP_EBUSY; // ESP-NOW has the MAC and the channel: nobody else
  if (want != RADIO_WANT_ESPNOW) return 0; // Wi-Fi page, net_up and BLE share as before (radio_share.h)
  return now.webshare || now.wifi_connected || now.ble ? APP_EBUSY : 0;
}

const char *radio_busy_reason(radio_users_t now) {
  if (now.espnow) return "ESP-NOW is already on";
  if (now.webshare) return "the Wi-Fi page";
  if (now.wifi_connected) return "Wi-Fi (net_up)";
  if (now.ble) return "a Bluetooth keyboard";
  return "";
}

bool espnow_mac_from_uid(const uint8_t uid[6], uint8_t mac[6]) {
  uint8_t m[6];
  memcpy(m, uid, 6);
  m[0] &= (uint8_t)~0x01;
  static const uint8_t zero[6] = {0};
  if (!memcmp(m, zero, 6)) return false;
  memcpy(mac, m, 6);
  return true;
}

int espnow_start_check(radio_users_t now, const uint8_t *mac, uint8_t channel) {
  uint8_t m[6];
  if (!mac || channel < 1 || channel > ESPNOW_CHANNEL_MAX || !espnow_mac_from_uid(mac, m)) return APP_EINVAL;
  return radio_claim_check(now, RADIO_WANT_ESPNOW);
}

void espnow_ring_init(espnow_ring_t *r, app_espnow_pkt_t *slots, uint32_t cap) {
  memset(r, 0, sizeof(*r));
  r->slots = slots;
  while (cap & (cap - 1)) cap &= cap - 1; // a power of two: index % cap stays in order when the counters wrap
  r->cap = cap;
}

bool espnow_ring_push(espnow_ring_t *r, const uint8_t src[6], int rssi, const uint8_t *data, int len) {
  uint32_t h = r->head;
  if (len < 0 || len > APP_ESPNOW_MAX || (len && !data) || h - __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE) >= r->cap) {
    __atomic_store_n(&r->dropped, r->dropped + 1, __ATOMIC_RELAXED);
    return false;
  }
  app_espnow_pkt_t *p = &r->slots[h % r->cap];
  memcpy(p->src, src, 6);
  p->rssi = (int8_t)(rssi < -128 ? -128 : rssi > 127 ? 127 : rssi);
  p->len = (uint8_t)len;
  if (len) memcpy(p->data, data, (size_t)len);
  __atomic_store_n(&r->received, r->received + 1, __ATOMIC_RELAXED);
  __atomic_store_n(&r->head, h + 1, __ATOMIC_RELEASE);
  return true;
}

bool espnow_ring_pop(espnow_ring_t *r, app_espnow_pkt_t *out) {
  uint32_t t = r->tail;
  if (t == __atomic_load_n(&r->head, __ATOMIC_ACQUIRE)) return false;
  *out = r->slots[t % r->cap];
  __atomic_store_n(&r->tail, t + 1, __ATOMIC_RELEASE);
  return true;
}

uint32_t espnow_ring_drops_to_log(espnow_ring_t *r, uint32_t now_ms, uint32_t every_ms, uint32_t *last_ms) {
  uint32_t d = __atomic_load_n(&r->dropped, __ATOMIC_RELAXED);
  uint32_t fresh = d - r->dropped_logged;
  if (!fresh || (*last_ms && now_ms - *last_ms < every_ms)) return 0;
  r->dropped_logged = d;
  *last_ms = now_ms ? now_ms : 1; // 0 means "never logged"
  return fresh;
}

void espnow_session_begin(espnow_session_t *s, const uint8_t orig_mac[6], uint8_t orig_protocol) {
  s->on = true;
  memcpy(s->orig_mac, orig_mac, 6);
  s->orig_protocol = orig_protocol;
}

bool espnow_session_end(espnow_session_t *s, uint8_t mac[6], uint8_t *protocol) {
  if (!s->on) return false;
  s->on = false;
  memcpy(mac, s->orig_mac, 6);
  *protocol = s->orig_protocol;
  return true;
}
