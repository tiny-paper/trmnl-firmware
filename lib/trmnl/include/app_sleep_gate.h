#pragma once

// While an app runs the app runner owns the device: TRMNL's sleep (goToSleep: deep sleep, light sleep for the
// display, a scheduler-driven restart) is refused until the app has ended. The runner closes the gate when the app
// ends, before it sleeps itself (docs/apps.md).

enum app_sleep_verdict_t {
  APP_SLEEP_GO,      // nothing runs: sleep
  APP_SLEEP_REFUSED, // an app runs: the caller goes on, the app keeps the device
};

void app_sleep_gate_app_started(const char *name);
void app_sleep_gate_app_ended(void);
// The app running now, or nullptr
const char *app_sleep_gate_app(void);
// A sleep or restart asked for `reason`
app_sleep_verdict_t app_sleep_gate_ask(const char *reason);
