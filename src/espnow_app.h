#pragma once

// ESP-NOW receive for apps (app_api.h API 2.4, docs/apps.md "ESP-NOW"): the firmware side. The rules are in
// lib/trmnl espnow_rx.h; one session at a time, for the running app.

#include <app_api.h>
#include <espnow_rx.h>

int espnow_app_start(const uint8_t mac[6], uint8_t channel);
int espnow_app_recv(app_espnow_pkt_t *pkt);
int espnow_app_stop(void); // safe when off; the runner calls it when the app ends
bool espnow_app_running(void);
// Who holds the radio now (the Wi-Fi page, the station's network, the Bluetooth keyboard, ESP-NOW)
radio_users_t espnow_app_radio_users(void);
