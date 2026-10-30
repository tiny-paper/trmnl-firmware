#include "app_sleep_gate.h"

#include <stddef.h>

static const char *running = nullptr;

void app_sleep_gate_app_started(const char *name) { running = name ? name : "?"; }

void app_sleep_gate_app_ended(void) { running = nullptr; }

const char *app_sleep_gate_app(void) { return running; }

app_sleep_verdict_t app_sleep_gate_ask(const char *reason) {
  (void)reason;
  return running ? APP_SLEEP_REFUSED : APP_SLEEP_GO;
}
