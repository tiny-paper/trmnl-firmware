#include "captive_ap.h"

#include <WiFi.h>
#include <esp_wifi.h>

void captive_ap_up(const IPAddress &ip, const String &ssid, const char *password, DNSServer &dns) {
  WiFi.mode(WIFI_MODE_AP);
  WiFi.disconnect();
  delay(50);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  delay(50);
  WiFi.softAP(ssid.c_str(), password, CAPTIVE_AP_CHANNEL, 0, CAPTIVE_AP_MAX_CLIENTS);
  delay(50);

  // Disable AMPDU RX on the ESP32 WiFi to fix a bug on Android
  esp_wifi_stop();
  esp_wifi_deinit();
  wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
  my_config.ampdu_rx_enable = false;
  esp_wifi_init(&my_config);
  esp_wifi_start();
  vTaskDelay(100 / portTICK_PERIOD_MS);

  dns.setTTL(3600);
  dns.start(53, "*", ip);
}

void captive_ap_routes(AsyncWebServer &server, const char *url) {
  // WARNING IOS (and maybe macos) WILL NOT POP UP IF IT CONTAINS THE WORD "Success"
  // Safari (iOS): the pop-up has no JavaScript or cookies in some versions; the page is still reachable by address.
  String target = url;
  server.on("/connecttest.txt", [](AsyncWebServerRequest *request) {
    request->redirect("http://logout.net");
  }); // windows 11 captive portal workaround
  // a 404 stops Windows 10 asking again and again (it panicked the ESP32)
  server.on("/wpad.dat", [](AsyncWebServerRequest *request) { request->send(404); });
  for (const char *path : {"/generate_204", "/redirect", "/hotspot-detect.html", "/canonical.html", "/ncsi.txt"}) {
    // android, microsoft, apple, firefox, windows
    server.on(path, [target](AsyncWebServerRequest *request) { request->redirect(target); });
  }
  server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); }); // firefox
  server.on("/favicon.ico", [](AsyncWebServerRequest *request) { request->send(404); });
}
