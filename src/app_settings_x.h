#pragma once

// The running app's settings form (API 2.6, app_settings_form.h): kept in PSRAM while the app runs, shown and saved by
// the Wi-Fi page's settings profile (webshare_settings.cpp).

#include <app_api.h>
#include <app_settings_form.h>

int app_settings_x_declare(const app_setting_decl_t *decls, uint32_t n); // settings_declare
bool app_settings_x_declared(void); // the app declared at least one field
// A copy of the form into *out (the page reads it while the app may declare again); false when none is declared
bool app_settings_x_copy(asf_form_t *out);
void app_settings_x_changed(int n); // APP_EVENT_SETTINGS with key n to the app (n > 0)
void app_settings_x_end(void); // the app ended (or starts): the form is gone
