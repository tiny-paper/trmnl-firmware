// ESP-NOW receive for apps (espnow_app.h). Mirrors the ELRS backpack's setup (Vrx_main.cpp SetSoftMACAddress and
// SetupEspNow): station mode, protocol 11b/g/n plus LR, the UID as station MAC, then esp_now_init and a receive
// callback. Receive only, so no peer is added (a peer is needed to send). Nothing here is kept across a restart: the
// MAC and protocol live in the Wi-Fi driver's RAM, and a boot after a crash starts from the eFuse MAC again.
#ifdef BOARD_TRMNL_X

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <trmnl_log.h>

#include "ble_keyboard.h"
#include "espnow_app.h"
#include "evlog.h"
#include "webshare.h"

#define ESPNOW_SLOTS 64 // 64 x 258 bytes in PSRAM, allocated at the first start and kept (no free under the callback)
#define ESPNOW_DROP_LOG_MS 10000

static espnow_ring_t ring;
static app_espnow_pkt_t *slots;
static espnow_session_t session;
static volatile bool running;
static uint32_t drop_logged_at;
static uint8_t channel_on;

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!running || !info || !info->src_addr) return;
  espnow_ring_push(&ring, info->src_addr, info->rx_ctrl ? info->rx_ctrl->rssi : 0, data, len);
}

// esp_wifi_set_mac wants the interface down: try it as it is, else stop, set, start
static esp_err_t set_station_mac(const uint8_t mac[6]) {
  esp_err_t rc = esp_wifi_set_mac(WIFI_IF_STA, mac);
  if (rc == ESP_OK) return rc;
  esp_wifi_stop();
  rc = esp_wifi_set_mac(WIFI_IF_STA, mac);
  esp_err_t st = esp_wifi_start();
  return rc != ESP_OK ? rc : st;
}

radio_users_t espnow_app_radio_users(void) {
  app_webshare_status_t ws;
  webshare_status(&ws);
  radio_users_t u = {};
  u.webshare = ws.running;
  u.wifi_connected = WiFi.status() == WL_CONNECTED;
  u.ble = ble_keyboard_radio_on();
  u.espnow = running;
  return u;
}

bool espnow_app_running(void) { return running; }

static void radio_back(void) {
  uint8_t mac[6], proto;
  if (espnow_session_end(&session, mac, &proto)) {
    esp_wifi_set_protocol(WIFI_IF_STA, proto);
    esp_err_t rc = set_station_mac(mac);
    if (rc != ESP_OK) Log_event("ESP-NOW: original MAC not set back (0x%x), a restart sets it", (unsigned)rc);
  }
  WiFi.mode(WIFI_OFF);
}

int espnow_app_start(const uint8_t mac[6], uint8_t channel) {
  radio_users_t u = espnow_app_radio_users();
  int rc = espnow_start_check(u, mac, channel);
  if (rc == APP_EBUSY) Log_event("ESP-NOW: refused, the radio is held by %s", radio_busy_reason(u));
  if (rc) return rc;
  if (!slots) slots = (app_espnow_pkt_t *)heap_caps_malloc(sizeof(app_espnow_pkt_t) * ESPNOW_SLOTS,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!slots) return APP_ENOMEM;
  uint8_t want[6], orig[6], proto = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
  espnow_mac_from_uid(mac, want);
  if (esp_read_mac(orig, ESP_MAC_WIFI_STA) != ESP_OK) return APP_EIO; // the eFuse one, whatever was set before
  if (!WiFi.mode(WIFI_STA)) return APP_EIO;
  WiFi.disconnect(false, false);
  WiFi.setSleep(false); // modem sleep would miss frames
  esp_wifi_get_protocol(WIFI_IF_STA, &proto);
  espnow_session_begin(&session, orig, proto);
  esp_err_t e = set_station_mac(want);
  const char *step = "mac";
  if (e == ESP_OK) {
    step = "protocol";
    e = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  }
  if (e == ESP_OK) {
    step = "channel";
    e = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  }
  if (e == ESP_OK) {
    step = "init";
    espnow_ring_init(&ring, slots, ESPNOW_SLOTS);
    drop_logged_at = 0;
    e = esp_now_init();
  }
  if (e == ESP_OK) {
    running = true;
    e = esp_now_register_recv_cb(on_recv);
    if (e != ESP_OK) {
      running = false;
      esp_now_deinit();
    }
  }
  if (e != ESP_OK) {
    Log_event("ESP-NOW: start failed at %s (0x%x)", step, (unsigned)e);
    radio_back();
    return e == ESP_ERR_NO_MEM ? APP_ENOMEM : APP_EIO;
  }
  channel_on = channel;
  Log_event("ESP-NOW: on, channel %u (receive only)", (unsigned)channel); // never the MAC: it is the bind UID
  return 0;
}

int espnow_app_recv(app_espnow_pkt_t *pkt) {
  if (!pkt) return APP_EINVAL;
  if (!running) return 0;
  uint32_t d = espnow_ring_drops_to_log(&ring, millis(), ESPNOW_DROP_LOG_MS, &drop_logged_at);
  if (d) Log_event("ESP-NOW: %lu frames dropped (queue full or too long)", (unsigned long)d);
  return espnow_ring_pop(&ring, pkt) ? 1 : 0;
}

int espnow_app_stop(void) {
  if (!running && !session.on) return 0;
  running = false;
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  radio_back();
  int dropped = (int)ring.dropped;
  Log_event("ESP-NOW: off, channel %u, %lu frames, %d dropped", (unsigned)channel_on, (unsigned long)ring.received,
            dropped);
  return dropped;
}

#endif // BOARD_TRMNL_X
