#pragma once

// The writer's Wi-Fi page (app_api.h "Wi-Fi page", docs/apps.md): the firmware side. One at a time, for the app
// that is running (its folder is app_host.dir). The logic is in lib/trmnl/webshare_*.

#include <app_api.h>

int webshare_start(app_webshare_mode_t mode);
void webshare_stop(void); // safe when not running, from any task except the page's own
int webshare_status(app_webshare_status_t *st);
