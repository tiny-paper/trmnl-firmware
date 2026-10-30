#pragma once
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

// The access point and the "is there internet?" answers a captive portal needs, shared by the Wi-Fi setup portal
// (WifiCaptive) and the writer's Wi-Fi page (src/webshare.cpp).

#define CAPTIVE_AP_CHANNEL 6
#define CAPTIVE_AP_MAX_CLIENTS 4

// Starts the soft AP `ssid` (password nullptr: open) on `ip` /24, with the Android AMPDU workaround, and a DNS
// server on `dns` that answers every name with `ip`. Blocks about half a second.
void captive_ap_up(const IPAddress &ip, const String &ssid, const char *password, DNSServer &dns);

// The probe URLs phones and PCs call to detect a captive portal; they redirect to `url` (e.g. "http://4.3.2.1")
void captive_ap_routes(AsyncWebServer &server, const char *url);
