#pragma once

// Shared by webshare.cpp (server, writer page) and webshare_install.cpp (apps page of "Install via Wi-Fi").
#ifdef BOARD_TRMNL_X

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <webshare_auth.h>
#include <webshare_store.h>

#include "webshare.h"

struct webshare_state_t {
  bool running;
  bool install; // the apps profile
  bool watch_alive;
  app_webshare_mode_t mode;
  ws_auth_t auth;
  uint32_t last_request;
  AsyncWebServer *server;
  DNSServer *dns;
  SemaphoreHandle_t lock; // start/stop and the upload state; handlers run in the async_tcp task
  char network[33], address[40], ip[16], url[32];
};
extern webshare_state_t S;
extern ws_upload_t upload;
extern const void *upload_owner; // the request that began `upload`
extern uint32_t upload_last_chunk;

typedef struct { // per upload request (AsyncWebServerRequest::_tempObject, freed by the library)
  ws_result_t rc;
  bool began, done, unauth;
  ws_put_t how;
  char final_path[WS_PATH_MAX + 1];
  char reason[64]; // apps: the ELF checker's reason when rc is WS_E_REFUSED
  char installed[48]; // apps: the file name kept
} up_req_t;

void send_json(AsyncWebServerRequest *r, int code, const String &body);
void send_error(AsyncWebServerRequest *r, int code, const char *what);
String param(AsyncWebServerRequest *r, const char *name, bool post = false);
int http_code(ws_result_t rc);
String session_cookie(AsyncWebServerRequest *r);

// Feeds one piece of an upload (writer: raw body of /api/put; apps: raw body of /api/app or one multipart file of
// /apps). `name`: the document path, or the app's file name. `exact`: `total` is the real byte count.
void webshare_feed_upload(AsyncWebServerRequest *r, const char *name, uint8_t *data, size_t len, size_t index,
                          size_t total, bool last, bool exact);

// The apps profile (webshare_install.cpp)
void webshare_install_routes(AsyncWebServer &server);
const char *webshare_install_page(void);
// Is the request allowed to add/remove apps: cookie plus X-Writer header, or ?t=<token>, or ?code=<digits>
bool webshare_install_allowed(AsyncWebServerRequest *r);
// Ends an apps upload (checker, .bak); fills q
void webshare_install_finish(up_req_t *q, bool exact);

#endif
