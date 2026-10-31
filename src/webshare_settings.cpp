// The settings profile of the Wi-Fi page (API 2.6, app_api.h "webshare_start_settings") and the X's keyboard layout,
// which every profile's page shows. Server and login are webshare.cpp's; the form's rules are lib/trmnl's
// app_settings_form, the layouts kbd_layout. No value of a field is ever logged; a secret is never sent back.
#ifdef BOARD_TRMNL_X

#include <esp_heap_caps.h>
#include <kbd_layout.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_host.h"
#include "app_settings_x.h"
#include "evlog.h"
#include "kbd_layout_x.h"
#include "webshare_internal.h"
#include "webshare_kbd_js.h"
#include "webshare_settings_page.h"

const char *webshare_settings_page(void) { return WEBSHARE_SETTINGS_PAGE; }

// ---- keyboard layout (every profile) ----

static void handle_kbd_get(AsyncWebServerRequest *r) {
  if (!webshare_authorized(r, false)) return send_error(r, 401, "type the code first");
  JsonDocument d;
  d["cur"] = kbd_layout_code(kbd_layout_x_current());
  JsonArray a = d["layouts"].to<JsonArray>();
  for (int i = 0; i < KBD_LAYOUT_COUNT; i++) {
    JsonObject o = a.add<JsonObject>();
    o["c"] = kbd_layout_code(i);
    o["n"] = kbd_layout_name(i);
  }
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

static void handle_kbd_set(AsyncWebServerRequest *r) {
  if (!webshare_authorized(r, true)) return send_error(r, 401, "type the code first");
  int l = kbd_layout_from_code(param(r, "c", true).c_str());
  if (l < 0) return send_error(r, 400, "unknown keyboard layout");
  if (!kbd_layout_x_store(l)) return send_error(r, 500, "could not save the layout");
  evlog_add("Keyboard layout: %s", kbd_layout_name(l));
  send_json(r, 200, "{\"ok\":true}");
}

void webshare_kbd_routes(AsyncWebServer &server) {
  server.on("/kbd.js", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse(200, "text/javascript; charset=utf-8", WEBSHARE_KBD_JS);
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
  });
  server.on("/api/kbd", HTTP_GET, handle_kbd_get);
  server.on("/api/kbd", HTTP_POST, handle_kbd_set);
}

// ---- the app's settings form ----

static const char *kind_name(uint8_t k) {
  return k == APP_SETTING_SECRET ? "secret" : k == APP_SETTING_NUMBER ? "number" : k == APP_SETTING_CHOICE ? "choice" : "text";
}

// The form, copied for this request (PSRAM; the app may declare again meanwhile)
static asf_form_t *form_copy(void) {
  asf_form_t *f = (asf_form_t *)heap_caps_malloc(sizeof(asf_form_t), MALLOC_CAP_SPIRAM);
  if (f && !app_settings_x_copy(f)) f->n = 0;
  return f;
}

static void handle_settings_get(AsyncWebServerRequest *r) {
  if (!webshare_authorized(r, false)) return send_error(r, 401, "type the code first");
  asf_form_t *form = form_copy();
  if (!form) return send_error(r, 500, "no memory");
  JsonDocument d;
  JsonArray a = d["fields"].to<JsonArray>();
  for (int i = 0; i < form->n; i++) {
    const asf_field_t *f = &form->f[i];
    JsonObject o = a.add<JsonObject>();
    o["k"] = f->key;
    o["l"] = f->label;
    o["t"] = kind_name(f->kind);
    o["min"] = f->min;
    o["max"] = f->max;
    if (f->kind == APP_SETTING_CHOICE) {
      JsonArray c = o["choices"].to<JsonArray>();
      char one[ASF_CHOICES_BYTES + 1];
      for (int k = 0; asf_choice(f, k, one, sizeof(one)); k++) c.add(one);
    }
    char stored[ASF_VALUE_MAX + 1], shown[ASF_VALUE_MAX + 1];
    int n = app_host_setting_get(f->key, stored, ASF_VALUE_MAX);
    bool set = false;
    asf_shown(f, stored, n > 0 ? (size_t)n : 0, shown, sizeof(shown), &set); // a secret: "" always
    if (f->kind != APP_SETTING_SECRET) o["v"] = shown;
    o["set"] = set;
  }
  free(form);
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

// Every value checked first, then written: one bad value writes nothing
static void handle_settings_set(AsyncWebServerRequest *r) {
  if (!webshare_authorized(r, true)) return send_error(r, 401, "type the code first");
  asf_form_t *form = form_copy();
  if (!form) return send_error(r, 500, "no memory");
  char why[ASF_WHY_MAX], msg[ASF_LABEL_MAX + ASF_WHY_MAX + 4];
  for (int i = 0; i < form->n; i++) {
    const asf_field_t *f = &form->f[i];
    String key = String("v_") + f->key;
    if (!r->hasParam(key, true)) continue; // not sent: unchanged (an empty secret field)
    String v = param(r, key.c_str(), true);
    if (asf_check(f, v.c_str(), v.length(), why, sizeof(why)) != 0) {
      snprintf(msg, sizeof(msg), "%s: %s", f->label, why);
      free(form);
      return send_error(r, 400, msg);
    }
  }
  int changed = 0, failed = 0;
  for (int i = 0; i < form->n; i++) {
    const asf_field_t *f = &form->f[i];
    String key = String("v_") + f->key;
    if (!r->hasParam(key, true)) continue;
    String v = param(r, key.c_str(), true);
    char stored[ASF_VALUE_MAX + 1];
    int n = app_host_setting_get(f->key, stored, ASF_VALUE_MAX);
    if (n < 0) n = 0;
    if ((size_t)n == v.length() && !memcmp(stored, v.c_str(), v.length())) continue;
    int rc = v.length() ? app_host_setting_set(f->key, v.c_str(), v.length()) : app_host_setting_remove(f->key);
    if (rc == 0) changed++;
    else failed++;
  }
  free(form);
  Log_info("webshare: settings saved, %d changed, %d failed", changed, failed);
  if (changed) app_settings_x_changed(changed);
  if (failed) return send_error(r, 500, "could not save every value (flash full?)");
  JsonDocument d;
  d["ok"] = true;
  d["changed"] = changed;
  String s;
  serializeJson(d, s);
  send_json(r, 200, s);
}

void webshare_settings_routes(AsyncWebServer &server) {
  server.on("/api/touch", HTTP_POST, webshare_handle_touch);
  server.on("/api/settings", HTTP_GET, handle_settings_get);
  server.on("/api/settings", HTTP_POST, handle_settings_set);
}

int webshare_start_settings(app_webshare_mode_t mode) {
  if (!app_settings_x_declared()) return APP_ENOENT;
  return webshare_start_profile(mode, WS_PROFILE_SETTINGS);
}

#endif // BOARD_TRMNL_X
