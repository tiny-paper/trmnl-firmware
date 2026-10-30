#pragma once

#include <stdbool.h>

#include <app_api.h>

// Which network the Wi-Fi page uses. AUTO (the default) is the home network when a saved one is reachable, else
// the own access point "Writer". HOME refuses to fall back (false: no network); AP is always the access point.
// `picked` is APP_WEBSHARE_HOME or APP_WEBSHARE_AP.
bool ws_pick_mode(app_webshare_mode_t requested, bool home_wifi_up, app_webshare_mode_t *picked);
