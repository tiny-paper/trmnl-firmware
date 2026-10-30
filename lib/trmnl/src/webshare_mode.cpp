#include "webshare_mode.h"

bool ws_pick_mode(app_webshare_mode_t requested, bool home_wifi_up, app_webshare_mode_t *picked) {
  switch (requested) {
  case APP_WEBSHARE_AUTO:
    *picked = home_wifi_up ? APP_WEBSHARE_HOME : APP_WEBSHARE_AP;
    return true;
  case APP_WEBSHARE_HOME:
    *picked = APP_WEBSHARE_HOME;
    return home_wifi_up;
  case APP_WEBSHARE_AP:
    *picked = APP_WEBSHARE_AP;
    return true;
  }
  return false;
}
