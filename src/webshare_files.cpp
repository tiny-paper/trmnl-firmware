// The files profile of the Wi-Fi page (API 2.3, app_api.h "webshare_start_files"): an app's own files of one folder, e.g.
// the Game Boy app's ROMs. Server, login, uploads and the no-overwrite rules are webshare.cpp's; the name, extension
// and size rules are lib/trmnl/webshare_files.
#ifdef BOARD_TRMNL_X

#include <string.h>

#include <trmnl_log.h>
#include <webshare_files.h>

#include "webshare_files_page.h"
#include "webshare_internal.h"

#define WS_FILES_LIST_MAX 100

ws_files_cfg_t files_cfg;

const char *webshare_files_page(void) { return WEBSHARE_FILES_PAGE; }

static bool on_file(const char *name, uint32_t size, void *ctx) {
  JsonObject o = (*(JsonArray *)ctx).add<JsonObject>();
  o["n"] = name;
  o["s"] = size;
  return true;
}

static void handle_files(AsyncWebServerRequest *r) {
  if (!webshare_authorized(r, false)) return send_error(r, 401, "type the code first");
  JsonDocument d;
  JsonArray files = d["files"].to<JsonArray>();
  ws_files_list(webshare_fs_app(), &files_cfg, WS_FILES_LIST_MAX, on_file, &files);
  JsonArray exts = d["exts"].to<JsonArray>();
  for (int i = 0; i < files_cfg.n_exts; i++) exts.add(files_cfg.exts[i]);
  d["max"] = files_cfg.max_bytes;
  d["free"] = (uint32_t)webshare_free_bytes();
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

static void send_why(AsyncWebServerRequest *r, ws_result_t rc) {
  char why[96];
  ws_files_why(&files_cfg, rc, why, sizeof(why));
  send_error(r, http_code(rc), why);
}

static void handle_file_delete(AsyncWebServerRequest *r) {
  if (!webshare_authorized(r, true)) return send_error(r, 401, "type the code first");
  ws_result_t rc = ws_files_delete(webshare_fs_app(), &files_cfg, param(r, "name").c_str());
  if (rc == WS_OK) return send_json(r, 200, "{\"ok\":true}");
  send_why(r, rc);
}

// The answer of an upload: {"ok":true,"how":"new|same|both","name":"<file name kept>"} or {"error":"<why>"}
static void handle_file_done(AsyncWebServerRequest *r) {
  up_req_t *q = (up_req_t *)r->_tempObject;
  if (!q || q->unauth) return send_error(r, 401, "type the code first");
  if (q->rc != WS_OK || !q->done) return send_why(r, q->rc == WS_OK ? WS_E_SHORT : q->rc);
  JsonDocument d;
  d["ok"] = true;
  d["how"] = q->how == WS_PUT_NEW ? "new" : q->how == WS_PUT_SAME ? "same" : "both";
  const char *slash = strrchr(q->final_path, '/');
  d["name"] = slash ? slash + 1 : q->final_path;
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

void webshare_files_routes(AsyncWebServer &server) {
  server.on("/api/touch", HTTP_POST, webshare_handle_touch);
  server.on("/api/files", HTTP_GET, handle_files);
  server.on("/api/filedelete", HTTP_POST, handle_file_delete);
  server.on("/api/file", HTTP_POST, handle_file_done, nullptr,
            [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
              webshare_feed_upload(r, param(r, "name").c_str(), data, len, index, total, index + len == total, true);
            });
}

int webshare_start_files(app_webshare_mode_t mode, const app_webshare_files_t *files) {
  if (!files || !files->dir || !files->exts) return APP_EINVAL;
  if (strnlen(files->dir, 64) >= 64 || strnlen(files->exts, 96) >= 96) return APP_EINVAL;
  ws_files_cfg_t c;
  if (!ws_files_cfg_set(&c, files->dir, files->exts, files->max_bytes)) return APP_EINVAL;
  webshare_stop(); // the old page is down before its settings change
  files_cfg = c;
  return webshare_start_profile(mode, WS_PROFILE_FILES);
}

#endif // BOARD_TRMNL_X
