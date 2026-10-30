// Bluetooth LE on, then net_up twice: the saved 2.4 GHz network joins while BLE shares the radio
#include <app_api.h>

APP_INFO("NetBLE", 0);

int app_main(const app_api_t *api) {
  if (api->keyboard_enable(APP_KBD_BLE) != 0) return 20;
  api->sleep_ms(2000); // NimBLE in sync and scanning
  bool first = api->net_up();
  bool again = api->net_up();
  api->log(first && again ? "netble: up" : "netble: down");
  api->keyboard_enable(0);
  return first && again ? 0 : 21;
}
