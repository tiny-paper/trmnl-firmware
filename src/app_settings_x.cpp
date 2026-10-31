// The running app's settings form (app_settings_x.h)
#ifdef BOARD_TRMNL_X

#include <esp_heap_caps.h>
#include <string.h>

#include "app_host.h"
#include "app_settings_x.h"

static asf_form_t *form; // PSRAM, allocated at the first declare and kept (no free while the page may read)
static SemaphoreHandle_t lock;

struct form_lock {
  form_lock() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~form_lock() { xSemaphoreGive(lock); }
};

static bool ready(void) {
  if (!lock) lock = xSemaphoreCreateMutex();
  if (lock && !form) {
    form = (asf_form_t *)heap_caps_calloc(1, sizeof(asf_form_t), MALLOC_CAP_SPIRAM);
    if (!form) form = (asf_form_t *)calloc(1, sizeof(asf_form_t));
  }
  return lock && form;
}

int app_settings_x_declare(const app_setting_decl_t *decls, uint32_t n) {
  if (!ready()) return APP_ENOMEM;
  form_lock l;
  return asf_declare(form, decls, n);
}

bool app_settings_x_declared(void) {
  if (!lock || !form) return false;
  form_lock l;
  return form->n > 0;
}

bool app_settings_x_copy(asf_form_t *out) {
  if (!out || !lock || !form) return false;
  form_lock l;
  memcpy(out, form, sizeof(*out));
  return out->n > 0;
}

void app_settings_x_changed(int n) {
  if (n <= 0 || !app_host.events) return;
  app_event_t ev = {};
  ev.type = APP_EVENT_SETTINGS;
  ev.key = (uint16_t)n;
  xQueueSend(app_host.events, &ev, 0);
}

void app_settings_x_end(void) {
  if (!lock || !form) return;
  form_lock l;
  memset(form, 0, sizeof(*form));
}

#endif // BOARD_TRMNL_X
