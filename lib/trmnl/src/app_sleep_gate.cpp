#include "app_sleep_gate.h"

#include <stddef.h>

static const char *running = nullptr;
static const char *screen = nullptr;

void app_sleep_gate_app_started(const char *name) { running = name ? name : "?"; }

void app_sleep_gate_app_ended(void) { running = nullptr; }

void app_sleep_gate_screen(const char *name) { screen = name; }

const char *app_sleep_gate_screen_name(void) { return screen; }

const char *app_sleep_gate_app(void) { return running; }

app_sleep_verdict_t app_sleep_gate_ask(const char *reason) {
  (void)reason;
  return (running || screen) ? APP_SLEEP_REFUSED : APP_SLEEP_GO;
}
