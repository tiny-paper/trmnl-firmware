// The apps profile of the Wi-Fi page ("Install via Wi-Fi" in the Apps menu): list, add and delete the ELF files in
// /apps from a phone or `curl`. Server, login, uploads and the no-overwrite rules are webshare.cpp's; the checks and
// the .bak bookkeeping are lib/trmnl/webshare_apps + app_install. A proof of concept: no manifest, no permissions.
#ifdef BOARD_TRMNL_X

#include <Preferences.h>
#include <app_install.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <globals.h>
#include <preferences_persistence.h>
#include <trmnl_log.h>
#include <webshare_apps.h>

#include "webshare_install_page.h"
#include "webshare_internal.h"

const char *webshare_install_page(void) { return WEBSHARE_INSTALL_PAGE; }

bool webshare_install_allowed(AsyncWebServerRequest *r) {
  if (!S.running) return false;
  String cookie = session_cookie(r);
  // A cookie works for our own page only (the header another site's form cannot add)
  if (cookie.length() && r->hasHeader("X-Writer") && ws_auth_session_ok(&S.auth, cookie.c_str())) {
    S.last_request = millis();
    return true;
  }
  String t = param(r, "t"), code = param(r, "code");
  uint32_t rnd[2] = {esp_random(), esp_random()};
  char sid[WS_TOKEN_LEN + 1];
  bool ok = ws_auth_request(&S.auth, nullptr, t.c_str(), code.c_str(), millis(), rnd, sid);
  if (ok) S.last_request = millis();
  return ok;
}

void webshare_install_finish(up_req_t *q, bool exact) {
  uint8_t *buf = (uint8_t *)heap_caps_malloc(upload.got ? upload.got : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) {
    ws_upload_abort(&upload);
    q->rc = WS_E_NO_SPACE;
    return;
  }
  size_t free_mem = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  q->rc = ws_apps_end(&upload, webshare_fs_root(), exact, buf, upload.got, free_mem, q->reason, sizeof(q->reason),
                      q->installed, sizeof(q->installed));
  heap_caps_free(buf);
  if (q->rc == WS_OK) {
    PreferencesPersistence p(preferences);
    char name[32];
    if (app_install_name_ok(q->installed, name, sizeof(name)) && p.readString(APPI_NOTE_KEY, "") == name) {
      p.remove(APPI_NOTE_KEY); // a new version replaces the one whose start failed
    }
    Log_info("webshare: installed %s", q->installed);
  } else if (q->rc == WS_E_REFUSED) {
    Log_info("webshare: an upload was refused: %s", q->reason);
  }
}

// ---- routes ----

static bool on_app(const char *file, uint32_t size, int64_t mtime, bool has_bak, void *ctx) {
  JsonObject o = (*(JsonArray *)ctx).add<JsonObject>();
  o["n"] = file;
  o["s"] = size;
  o["d"] = (int64_t)mtime;
  o["bak"] = has_bak;
  return true;
}

static void handle_apps(AsyncWebServerRequest *r) {
  if (!S.running || !ws_auth_session_ok(&S.auth, session_cookie(r).c_str())) return send_error(r, 401, "type the code first");
  S.last_request = millis();
  JsonDocument d;
  JsonArray apps = d["apps"].to<JsonArray>();
  ws_apps_list(webshare_fs_root(), 40, on_app, &apps);
  PreferencesPersistence p(preferences);
  String note = p.readString(APPI_NOTE_KEY, "");
  if (note.length()) d["note"] = "The last start of " + note + " failed; the older version was put back.";
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

static void handle_app_delete(AsyncWebServerRequest *r) {
  if (!webshare_install_allowed(r)) return send_error(r, 401, "type the code first");
  ws_result_t rc = ws_apps_delete(webshare_fs_root(), param(r, "name").c_str());
  if (rc == WS_OK) return send_json(r, 200, "{\"ok\":true}");
  send_error(r, http_code(rc), ws_result_str(rc));
}

// The answer of an upload (page and curl): {"ok":true,"name":"x.elf"} or {"error":"<why>"}
static void handle_app_done(AsyncWebServerRequest *r) {
  up_req_t *q = (up_req_t *)r->_tempObject;
  if (!q) return send_error(r, 400, "no file in the request (curl -F file=@app.elf)");
  if (q->unauth) return send_error(r, 401, "type the code first");
  if (q->rc == WS_E_REFUSED) {
    String why = String("refused: ") + q->reason;
    return send_error(r, 400, why.c_str());
  }
  if (q->rc != WS_OK || !q->done) {
    ws_result_t rc = q->rc == WS_OK ? WS_E_SHORT : q->rc;
    return send_error(r, http_code(rc), ws_result_str(rc));
  }
  JsonDocument d;
  d["ok"] = true;
  d["name"] = q->installed;
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

void webshare_install_routes(AsyncWebServer &server) {
  server.on("/api/apps", HTTP_GET, handle_apps);
  server.on("/api/appdelete", HTTP_POST, handle_app_delete);
  // the page: the raw file as the body, its name in ?name=
  auto raw = [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
    webshare_feed_upload(r, param(r, "name").c_str(), data, len, index, total, index + len == total, true);
  };
  server.on("/api/app", HTTP_POST, handle_app_done, nullptr, raw);
  // curl -F file=@app.elf (multipart; the size is the whole body, an upper bound) or --data-binary with ?name=
  server.on("/apps", HTTP_POST, handle_app_done,
            [](AsyncWebServerRequest *r, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
              webshare_feed_upload(r, filename.c_str(), data, len, index, r->contentLength(), final, false);
            },
            raw);
}

#endif // BOARD_TRMNL_X
