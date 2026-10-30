// The writer's Wi-Fi page: an HTTP server on the X's own access point "Writer" or on the home network, for a phone
// browser. All decisions (names, code, no-overwrite, atomic writes, zip) are in lib/trmnl/webshare_*; this file
// only connects them to the network and LittleFS.
#ifdef BOARD_TRMNL_X

#include <app_sleep_gate.h>
#include "evlog.h"
#include "webshare.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <captive_ap.h>
#include <esp_random.h>
#include <trmnl_log.h>
#include <webshare_auth.h>
#include <webshare_mode.h>
#include <webshare_store.h>
#include <webshare_apps.h>
#include <webshare_zip.h>

#include <apps.h>

#include "app_host.h"
#include "webshare_internal.h"
#include "webshare_page.h"

#define WS_AP_URL "http://4.3.2.1"
#define WS_LIST_MAX 200

// ---- LittleFS under the app's folder ----

static char base_dir[48]; // the writer's folder; the install profile uses the root (ctx "")

static bool full(void *ctx, const char *rel, char *out, size_t len) {
  return snprintf(out, len, "%s/%s", (const char *)ctx, rel) < (int)len;
}

static int fs_stat(void *c, const char *path, uint32_t *size, bool *is_dir) {
  char p[128];
  if (!full(c, path, p, sizeof(p)) || !LittleFS.exists(p)) return -1;
  File f = LittleFS.open(p);
  if (!f) return -1;
  if (size) *size = (uint32_t)f.size();
  if (is_dir) *is_dir = f.isDirectory();
  f.close();
  return 0;
}

static void *fs_open(void *c, const char *path, bool write) {
  char p[128];
  if (!full(c, path, p, sizeof(p))) return nullptr;
  File *f = new (std::nothrow) File(LittleFS.open(p, write ? FILE_WRITE : FILE_READ));
  if (!f || !*f || f->isDirectory()) {
    delete f;
    return nullptr;
  }
  return f;
}

static int fs_read(void *, void *file, void *buf, size_t len) { return ((File *)file)->read((uint8_t *)buf, len); }

static int fs_write(void *, void *file, const void *buf, size_t len) {
  return ((File *)file)->write((const uint8_t *)buf, len) == len ? (int)len : -1;
}

static int fs_close(void *, void *file) {
  File *f = (File *)file;
  f->close();
  delete f;
  return 0; // the Arduino wrapper does not report errors; webshare_store checks the size on disk
}

static int fs_rename(void *c, const char *from, const char *to) {
  char a[128], b[128];
  return full(c, from, a, sizeof(a)) && full(c, to, b, sizeof(b)) && LittleFS.rename(a, b) ? 0 : -1;
}

static int fs_remove(void *c, const char *path) {
  char p[128];
  return full(c, path, p, sizeof(p)) && LittleFS.remove(p) ? 0 : -1;
}

static int fs_mkdir(void *c, const char *path) {
  char p[128];
  return full(c, path, p, sizeof(p)) && LittleFS.mkdir(p) ? 0 : -1;
}

static int fs_list(void *c, const char *dir, ws_list_cb_t cb, void *cx) {
  char p[128];
  if (!full(c, dir, p, sizeof(p))) return -1;
  File root = LittleFS.open(p);
  if (!root || !root.isDirectory()) return -1;
  int n = 0;
  while (File e = root.openNextFile()) {
    String name = e.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    bool go_on = cb(name.c_str(), e.isDirectory(), (uint32_t)e.size(), cx);
    e.close();
    n++;
    if (!go_on) break;
  }
  root.close();
  return n;
}

static uint64_t fs_free(void *) { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

static int64_t fs_mtime(void *c, const char *path) {
  char p[128];
  if (!full(c, path, p, sizeof(p))) return 0;
  File f = LittleFS.open(p);
  int64_t t = f ? (int64_t)f.getLastWrite() : 0;
  f.close();
  return t > 1700000000 ? t : 0; // before late 2023: the clock was not set
}

static const ws_fs_t FS = {(void *)base_dir, fs_stat, fs_open, fs_read, fs_write, fs_close, fs_rename, fs_remove,
                           fs_mkdir, fs_list, fs_free, fs_mtime};
static const ws_fs_t FS_ROOT = {(void *)"", fs_stat, fs_open, fs_read, fs_write, fs_close, fs_rename, fs_remove,
                                fs_mkdir, fs_list, fs_free, fs_mtime};

const ws_fs_t *webshare_fs_root(void) { return &FS_ROOT; }

// ---- state ----

webshare_state_t S;

ws_upload_t upload;
const void *upload_owner;
uint32_t upload_last_chunk;
static bool zip_busy;


// ---- request helpers ----

void send_json(AsyncWebServerRequest *r, int code, const String &body) {
  AsyncWebServerResponse *res = r->beginResponse(code, "application/json", body);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

void send_error(AsyncWebServerRequest *r, int code, const char *what) {
  JsonDocument d;
  d["error"] = what;
  String s;
  serializeJson(d, s);
  send_json(r, code, s);
}

String session_cookie(AsyncWebServerRequest *r) {
  if (!r->hasHeader("Cookie")) return String();
  String c = r->header("Cookie");
  int at = c.indexOf("wsid=");
  return at < 0 ? String() : c.substring(at + 5, at + 5 + WS_TOKEN_LEN);
}

static bool session_ok(AsyncWebServerRequest *r) { return ws_auth_session_ok(&S.auth, session_cookie(r).c_str()); }

// A request with a good session (and, for anything that changes data, the header only our page sends: another
// site's form cannot add it). Refreshes the idle timer.
static bool guard(AsyncWebServerRequest *r, bool changes, bool reply = true) {
  if (!S.running || !session_ok(r) || (changes && !r->hasHeader("X-Writer"))) {
    if (reply) send_error(r, 401, "type the code first");
    return false;
  }
  S.last_request = millis();
  return true;
}

String param(AsyncWebServerRequest *r, const char *name, bool post) {
  const AsyncWebParameter *p = r->getParam(name, post);
  return p ? p->value() : String();
}

int http_code(ws_result_t rc) {
  switch (rc) {
  case WS_OK: return 200;
  case WS_E_NAME: case WS_E_SHORT: return 400;
  case WS_E_NOT_FOUND: return 404;
  case WS_E_EXISTS: case WS_E_KEEP_FAILED: return 409;
  case WS_E_TOO_BIG: return 413;
  case WS_E_BUSY: return 503;
  case WS_E_NO_SPACE: return 507;
  default: return 500;
  }
}

// ---- routes ----

static bool on_list_doc(const char *path, uint32_t size, ws_kind_t kind, void *ctx) {
  JsonArray a = *(JsonArray *)ctx;
  (void)kind;
  JsonObject o = a.add<JsonObject>();
  o["p"] = path;
  o["s"] = size;
  return true;
}

static void handle_list(AsyncWebServerRequest *r) {
  if (!guard(r, false)) return;
  JsonDocument d;
  JsonArray docs = d["docs"].to<JsonArray>(), fonts = d["fonts"].to<JsonArray>();
  // documents first, then fonts: ws_store_list(fonts=true) lists fonts before documents, so list them apart
  struct Both { JsonArray docs, fonts; } both = {docs, fonts};
  ws_store_list(&FS, true, WS_LIST_MAX, [](const char *path, uint32_t size, ws_kind_t kind, void *ctx) {
    Both *b = (Both *)ctx;
    return on_list_doc(path, size, kind, kind == WS_FONT ? &b->fonts : &b->docs);
  }, &both);
  d["free"] = (uint32_t)fs_free(nullptr);
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

static void handle_login(AsyncWebServerRequest *r) {
  if (!S.running || !r->hasHeader("X-Writer")) return send_error(r, 401, "not available");
  S.last_request = millis();
  String pin = param(r, "pin", true);
  uint32_t rnd[2] = {esp_random(), esp_random()};
  char token[WS_TOKEN_LEN + 1];
  ws_auth_result_t res = ws_auth_login(&S.auth, pin.c_str(), millis(), rnd, token);
  if (res == WS_AUTH_OK) {
    AsyncWebServerResponse *out = r->beginResponse(200, "application/json", "{\"ok\":true}");
    out->addHeader("Set-Cookie", String("wsid=") + token + "; Path=/; HttpOnly; SameSite=Strict");
    out->addHeader("Cache-Control", "no-store");
    r->send(out);
  } else if (res == WS_AUTH_WRONG) {
    send_error(r, 403, "wrong code");
  } else if (res == WS_AUTH_LOCKED) {
    send_error(r, 429, "too many wrong codes: wait a minute or more");
  } else {
    send_error(r, 423, "too many wrong codes: switched off, start it again on the X");
  }
}

static void handle_get(AsyncWebServerRequest *r) {
  if (!guard(r, false)) return;
  String p = param(r, "p");
  char path[128];
  uint32_t size;
  bool dir;
  if (ws_classify(p.c_str()) != WS_DOC || fs_stat(base_dir, p.c_str(), &size, &dir) != 0 || dir ||
      !full(base_dir, p.c_str(), path, sizeof(path))) {
    return send_error(r, 404, "no such document");
  }
  AsyncWebServerResponse *res = r->beginResponse(LittleFS, path, "application/octet-stream", true);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

typedef struct {
  ws_zip_t z;
  ws_zip_entry_t entries[WS_ZIP_MAX_FILES];
  char paths[WS_ZIP_MAX_FILES][WS_PATH_MAX + 1];
  const char *ptrs[WS_ZIP_MAX_FILES];
  int n;
} zip_job_t;

static void handle_zip(AsyncWebServerRequest *r) {
  if (!guard(r, false)) return;
  if (zip_busy) return send_error(r, 503, "another zip is being made");
  zip_job_t *job = new (std::nothrow) zip_job_t;
  if (!job) return send_error(r, 503, "out of memory");
  job->n = 0;
  ws_store_list(&FS, false, WS_ZIP_MAX_FILES, [](const char *path, uint32_t, ws_kind_t, void *ctx) {
    zip_job_t *j = (zip_job_t *)ctx;
    strcpy(j->paths[j->n], path);
    j->ptrs[j->n] = j->paths[j->n];
    j->n++;
    return true;
  }, job);
  if (!ws_zip_begin(&job->z, &FS, job->entries, job->ptrs, job->n)) {
    delete job;
    return send_error(r, 500, "zip failed");
  }
  zip_busy = true;
  AsyncWebServerResponse *res = r->beginChunkedResponse(
      "application/zip", [job, r](uint8_t *buf, size_t max, size_t) -> size_t {
        int n = ws_zip_read(&job->z, buf, max);
        if (n < 0) { // a file could not be read: cut the connection so the phone sees a failed download
          Log_error("webshare: zip aborted, a file could not be read");
          r->client()->abort();
          return 0;
        }
        S.last_request = millis();
        return (size_t)n;
      });
  res->addHeader("Content-Disposition", "attachment; filename=\"" WS_HOST "-documents.zip\"");
  res->addHeader("Cache-Control", "no-store");
  r->onDisconnect([job]() {
    ws_zip_end(&job->z);
    delete job;
    zip_busy = false;
  });
  r->send(res);
}

static void handle_rename(AsyncWebServerRequest *r) {
  if (!guard(r, true)) return;
  ws_result_t rc = ws_store_rename(&FS, param(r, "from").c_str(), param(r, "to").c_str());
  if (rc == WS_OK) return send_json(r, 200, "{\"ok\":true}");
  send_error(r, http_code(rc), ws_result_str(rc));
}

static void handle_delete(AsyncWebServerRequest *r) {
  if (!guard(r, true)) return;
  ws_result_t rc = ws_store_delete(&FS, param(r, "p").c_str());
  if (rc == WS_OK) return send_json(r, 200, "{\"ok\":true}");
  send_error(r, http_code(rc), ws_result_str(rc));
}

// One piece of an upload. One upload at a time (the rest get 503). Writer: raw file bytes of POST /api/put?p=<path>.
void webshare_feed_upload(AsyncWebServerRequest *r, const char *name, uint8_t *data, size_t len, size_t index,
                          size_t total, bool last, bool exact) {
  up_req_t *q = (up_req_t *)r->_tempObject;
  if (index == 0) {
    q = (up_req_t *)calloc(1, sizeof(up_req_t));
    r->_tempObject = q;
    if (!q) return;
    if (S.install ? !webshare_install_allowed(r) : !guard(r, true, false)) {
      q->unauth = true;
      return;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    q->rc = S.install ? ws_apps_begin(&upload, &FS_ROOT, name, (uint32_t)total)
                      : ws_upload_begin(&upload, &FS, name, (uint32_t)total);
    q->began = q->rc == WS_OK;
    if (q->began) {
      upload_owner = r;
      upload_last_chunk = millis();
      r->onDisconnect([r]() { // cable pulled, phone gone: drop the temporary file
        xSemaphoreTake(S.lock, portMAX_DELAY);
        if (upload_owner == r) {
          ws_upload_abort(&upload);
          upload_owner = nullptr;
        }
        xSemaphoreGive(S.lock);
      });
    }
    xSemaphoreGive(S.lock);
  }
  if (!q || !q->began) return;
  xSemaphoreTake(S.lock, portMAX_DELAY);
  if (upload_owner == r) {
    S.last_request = upload_last_chunk = millis();
    q->rc = ws_upload_chunk(&upload, data, len);
    if (q->rc != WS_OK) {
      q->began = false;
      upload_owner = nullptr;
    } else if (last) {
      if (S.install) webshare_install_finish(q, exact);
      else q->rc = ws_upload_end(&upload, &q->how, q->final_path);
      q->began = false;
      q->done = true;
      upload_owner = nullptr;
    }
  } else {
    q->began = false; // the stall check dropped it
    q->rc = WS_E_SHORT;
  }
  xSemaphoreGive(S.lock);
}

static void handle_put_body(AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
  webshare_feed_upload(r, param(r, "p").c_str(), data, len, index, total, index + len == total, true);
}

static void handle_put(AsyncWebServerRequest *r) {
  up_req_t *q = (up_req_t *)r->_tempObject;
  if (!guard(r, true)) return;
  up_req_t empty = {};
  if (!q) { // no body: an empty file
    xSemaphoreTake(S.lock, portMAX_DELAY);
    empty.rc = ws_upload_begin(&upload, &FS, param(r, "p").c_str(), 0);
    if (empty.rc == WS_OK) empty.rc = ws_upload_end(&upload, &empty.how, empty.final_path);
    empty.done = empty.rc == WS_OK;
    xSemaphoreGive(S.lock);
    q = &empty;
  }
  if (q->rc != WS_OK || !q->done) {
    return send_error(r, http_code(q->rc == WS_OK ? WS_E_SHORT : q->rc), ws_result_str(q->rc == WS_OK ? WS_E_SHORT : q->rc));
  }
  JsonDocument d;
  d["ok"] = true;
  d["how"] = q->how == WS_PUT_NEW ? "new" : q->how == WS_PUT_SAME ? "same" : "both";
  d["path"] = q->final_path;
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

// ---- start, stop, status ----

static void stop_locked() {
  if (!S.running) return;
  S.running = false;
  if (upload_owner) { // a half upload: the temporary file goes
    ws_upload_abort(&upload);
    upload_owner = nullptr;
  }
  if (S.server) {
    S.server->end();
    delete S.server;
    S.server = nullptr;
  }
  if (S.mode == APP_WEBSHARE_AP) {
    if (S.dns) {
      S.dns->stop();
      delete S.dns;
      S.dns = nullptr;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  } else {
    MDNS.end();
  }
  Log_info("webshare: off");
  evlog_add("webshare: off");
}

// Switches the page off after WS_IDLE_MS without a request, and drops an upload that stopped coming
static void watch_task(void *) {
  while (S.running) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    xSemaphoreTake(S.lock, portMAX_DELAY);
    if (S.running && upload_owner && millis() - upload_last_chunk > WS_UPLOAD_STALL_MS) {
      Log_error("webshare: upload stalled, dropped");
      ws_upload_abort(&upload);
      upload_owner = nullptr;
    }
    // never while an upload runs: it would end as "no connection" in the browser
    bool idle = !upload_owner && ws_idle_expired(S.last_request, millis());
    if (S.running && (idle || S.auth.fails >= WS_MAX_FAILS)) {
      Log_info("webshare: idle or locked out, switching off");
      evlog_add("webshare: switched off, %s", idle ? "idle" : "too many wrong codes");
      stop_locked();
    }
    xSemaphoreGive(S.lock);
  }
  S.watch_alive = false;
  vTaskDelete(nullptr);
}

void webshare_stop(void) {
  if (!S.lock) return;
  xSemaphoreTake(S.lock, portMAX_DELAY);
  stop_locked();
  xSemaphoreGive(S.lock);
  for (int i = 0; i < 30 && S.watch_alive; i++) vTaskDelay(pdMS_TO_TICKS(100)); // the watcher leaves by itself
}

static void add_routes(AsyncWebServer &server) {
  if (S.mode == APP_WEBSHARE_AP) captive_ap_routes(server, WS_AP_URL);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (r->hasParam("t")) { // the one-time link from the X's QR: log in, then drop the token from the address
      uint32_t rnd[2] = {esp_random(), esp_random()};
      char sid[WS_TOKEN_LEN + 1];
      S.last_request = millis();
      if (ws_auth_token_login(&S.auth, param(r, "t").c_str(), millis(), rnd, sid) == WS_AUTH_OK) {
        AsyncWebServerResponse *go = r->beginResponse(302);
        go->addHeader("Location", "/");
        go->addHeader("Set-Cookie", String("wsid=") + sid + "; Path=/; HttpOnly; SameSite=Strict");
        go->addHeader("Cache-Control", "no-store");
        return r->send(go);
      }
    }
    AsyncWebServerResponse *res = r->beginResponse(200, "text/html; charset=utf-8", S.install ? webshare_install_page() : WEBSHARE_PAGE);
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
  });
  server.on("/api/login", HTTP_POST, handle_login);
  if (S.install) {
    webshare_install_routes(server);
    server.onNotFound([](AsyncWebServerRequest *r) {
      if (S.mode == APP_WEBSHARE_AP) r->redirect(WS_AP_URL);
      else r->send(404);
    });
    return;
  }
  server.on("/api/list", HTTP_GET, handle_list);
  server.on("/api/get", HTTP_GET, handle_get);
  server.on("/api/zip", HTTP_GET, handle_zip);
  server.on("/api/rename", HTTP_POST, handle_rename);
  server.on("/api/delete", HTTP_POST, handle_delete);
  server.on("/api/put", HTTP_POST, handle_put, nullptr, handle_put_body);
  server.onNotFound([](AsyncWebServerRequest *r) {
    if (S.mode == APP_WEBSHARE_AP) r->redirect(WS_AP_URL);
    else r->send(404);
  });
}

static int start_common(app_webshare_mode_t requested, bool install) {
  if (requested != APP_WEBSHARE_AUTO && requested != APP_WEBSHARE_AP && requested != APP_WEBSHARE_HOME) {
    return APP_EINVAL;
  }
  if (!S.lock) S.lock = xSemaphoreCreateMutex();
  if (!S.lock) return APP_ENOMEM;
  webshare_stop(); // a new start is a new code
  app_webshare_mode_t mode;
  bool wifi_up = requested == APP_WEBSHARE_AP ? false : app_host_net_up();
  if (!ws_pick_mode(requested, wifi_up, &mode)) return APP_ENONET;
  if (mode == APP_WEBSHARE_AP) Log_info("webshare: no home network, using the access point");

  xSemaphoreTake(S.lock, portMAX_DELAY);
  S.install = install;
  if (install) { // the file system root, the apps folder
    LittleFS.mkdir(APPS_DIR);
    ws_apps_cleanup(&FS_ROOT);
  } else {
    snprintf(base_dir, sizeof(base_dir), "%s", app_host.dir);
    LittleFS.mkdir(APPS_DATA_DIR);
    LittleFS.mkdir(base_dir);
    ws_store_cleanup(&FS); // temporary files a power loss left
  }
  memset(&S.auth, 0, sizeof(S.auth));
  ws_auth_init(&S.auth, esp_random());
  uint32_t tok[4] = {esp_random(), esp_random(), esp_random(), esp_random()};
  ws_auth_new_token(&S.auth, tok, millis());
  S.mode = mode;
  S.last_request = millis();
  S.network[0] = '\0';
  upload_owner = nullptr;
  zip_busy = false;
  int rc = 0;
  S.server = new (std::nothrow) AsyncWebServer(80);
  if (!S.server) {
    rc = APP_ENOMEM;
  } else if (mode == APP_WEBSHARE_AP) {
    S.dns = new (std::nothrow) DNSServer();
    if (!S.dns) {
      rc = APP_ENOMEM;
    } else {
      captive_ap_up(IPAddress(4, 3, 2, 1), WS_AP_SSID, nullptr, *S.dns);
      snprintf(S.network, sizeof(S.network), "%s", WS_AP_SSID);
      snprintf(S.address, sizeof(S.address), "%s", WS_AP_URL);
      snprintf(S.ip, sizeof(S.ip), "4.3.2.1");
    }
  } else {
    MDNS.end(); // the setup code started one with the device's own host name
    if (!MDNS.begin(WS_HOST)) Log_error("webshare: mDNS failed, use the address");
    snprintf(S.address, sizeof(S.address), "http://%s.local", WS_HOST);
    snprintf(S.ip, sizeof(S.ip), "%s", WiFi.localIP().toString().c_str());
  }
  if (rc == 0) {
    snprintf(S.url, sizeof(S.url), "http://%s/", S.ip);
    add_routes(*S.server);
    S.server->begin();
    S.running = true;
    S.watch_alive = true;
    if (xTaskCreate(watch_task, "webshare", 4096, nullptr, 1, nullptr) != pdPASS) {
      S.watch_alive = false;
      rc = APP_ENOMEM;
      stop_locked();
    }
  } else if (S.server) {
    delete S.server;
    S.server = nullptr;
    delete S.dns;
    S.dns = nullptr;
  }
  xSemaphoreGive(S.lock);
  if (rc == 0) Log_info("webshare: on, %s mode, %s", mode == APP_WEBSHARE_AP ? "access point" : "home network", S.address);
  return rc;
}

int webshare_start(app_webshare_mode_t requested) { return start_common(requested, false); }
int webshare_start_install(void) { return start_common(APP_WEBSHARE_AUTO, true); }

int webshare_status(app_webshare_status_t *st) {
  if (!st) return APP_EINVAL;
  memset(st, 0, sizeof(*st));
  if (!S.lock || !S.running) return 0;
  st->running = true;
  st->mode = S.mode;
  snprintf(st->url, sizeof(st->url), "%s", S.url);
  if (S.auth.login_token[0]) snprintf(st->login_url, sizeof(st->login_url), "%s?t=%s", S.url, S.auth.login_token);
  else snprintf(st->login_url, sizeof(st->login_url), "%s", S.url); // used up: the code is what is left
  snprintf(st->code, sizeof(st->code), "%s", S.auth.pin);
  snprintf(st->network, sizeof(st->network), "%s", S.network);
  snprintf(st->address, sizeof(st->address), "%s", S.address);
  snprintf(st->ip, sizeof(st->ip), "%s", S.ip);
  uint32_t used = millis() - S.last_request;
  st->idle_left_s = used >= WS_IDLE_MS ? 0 : (WS_IDLE_MS - used) / 1000;
  st->locked = S.auth.fails >= WS_MAX_FAILS ? 2 : ws_auth_lock_left(&S.auth, millis()) ? 1 : 0;
  return 0;
}

#endif // BOARD_TRMNL_X
