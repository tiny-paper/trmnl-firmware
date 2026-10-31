#pragma once

#include <app_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ESP-NOW receive for apps (API 2.4, docs/apps.md "ESP-NOW"): the pure rules. src/espnow_app.cpp carries them out.

// ---- the radio's users ----

// Who holds the S3's one 2.4 GHz radio now. Wi-Fi page, an app's net_up and a Bluetooth keyboard share it (radio_share.h);
// ESP-NOW changes the station MAC and the channel, so it shares with none of them
typedef struct {
  bool webshare; // the Wi-Fi page runs
  bool wifi_connected; // the station is on a network (an app's net_up or http)
  bool ble; // the Bluetooth keyboard's radio is on
  bool espnow; // ESP-NOW receive runs
} radio_users_t;

typedef enum { RADIO_WANT_WEBSHARE, RADIO_WANT_NET, RADIO_WANT_BLE, RADIO_WANT_ESPNOW } radio_want_t;

// 0 when `want` may start now, APP_EBUSY when another user holds the radio
int radio_claim_check(radio_users_t now, radio_want_t want);
// Why ESP-NOW was refused, for the event log ("the Wi-Fi page", "Wi-Fi (net_up)", "a Bluetooth keyboard",
// "ESP-NOW is already on"), "" when it was not
const char *radio_busy_reason(radio_users_t now);

// ---- start ----

#define ESPNOW_CHANNEL_MAX 13

// The station MAC for `uid`, as the ELRS backpack sets it: the UID with bit 0 of byte 0 cleared (a unicast address;
// the Wi-Fi driver refuses a multicast one). False (mac untouched) for a UID that gives 00:00:00:00:00:00
bool espnow_mac_from_uid(const uint8_t uid[6], uint8_t mac[6]);
// espnow_start's checks in order: APP_EINVAL (mac NULL, channel not 1..13, an all-zero MAC), APP_EBUSY (radio held), 0
int espnow_start_check(radio_users_t now, const uint8_t *mac, uint8_t channel);

// ---- the queue ----

// One producer (the Wi-Fi task's receive callback) and one consumer (the app's espnow_recv); no lock
typedef struct {
  app_espnow_pkt_t *slots;
  uint32_t cap;
  volatile uint32_t head, tail;
  volatile uint32_t received; // frames taken into the queue
  volatile uint32_t dropped; // frames lost: queue full, or longer than APP_ESPNOW_MAX
  uint32_t dropped_logged; // consumer side: drops already in the event log
} espnow_ring_t;

// `slots` holds `cap` packets (cap >= 1; rounded down to a power of two, so the order survives the counters wrapping);
// the counters start at 0
void espnow_ring_init(espnow_ring_t *r, app_espnow_pkt_t *slots, uint32_t cap);
// Producer: false when the frame was dropped (and counted)
bool espnow_ring_push(espnow_ring_t *r, const uint8_t src[6], int rssi, const uint8_t *data, int len);
// Consumer: true with *out filled, false when the queue is empty
bool espnow_ring_pop(espnow_ring_t *r, app_espnow_pkt_t *out);
// Consumer: the drops not yet reported, at most once per `every_ms` (0 when none or too soon); marks them reported
uint32_t espnow_ring_drops_to_log(espnow_ring_t *r, uint32_t now_ms, uint32_t every_ms, uint32_t *last_ms);

// ---- the session: what stop puts back ----

typedef struct {
  bool on;
  uint8_t orig_mac[6]; // the station MAC before start (the eFuse one)
  uint8_t orig_protocol; // the station's protocol bitmap before start
} espnow_session_t;

void espnow_session_begin(espnow_session_t *s, const uint8_t orig_mac[6], uint8_t orig_protocol);
// True once per begin, with what to put back; false when nothing is on (a second stop, the app's end after its stop)
bool espnow_session_end(espnow_session_t *s, uint8_t mac[6], uint8_t *protocol);
