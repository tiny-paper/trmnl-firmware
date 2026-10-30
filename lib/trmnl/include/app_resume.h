#pragma once

#include <persistence_interface.h>

// Which app to resume after a restart (docs/apps.md), kept in NVS. An app is only resumed once it
// ran long enough to count as started (app_resume_started): starting it clears the flag, so an app
// that crashes while starting is not started again, and the device goes back to TRMNL.

#define APP_RESUME_PATH_KEY "app_last"
#define APP_RESUME_FLAG_KEY "app_resume"

// The app to resume, if one is due: copies its path to `path` and clears the flag (a crash before
// app_resume_started() then ends the resuming). `crashed`: the last reset was a panic or watchdog;
// nothing is resumed and the flag is cleared.
bool app_resume_take(Persistence &p, bool crashed, char *path, size_t path_len);

// An app is being started (from the menu or resumed): remembered, not yet due for resuming
void app_resume_starting(Persistence &p, const char *path);

// The app has run for APP_RESUME_STARTED_MS: resume it after a restart
void app_resume_started(Persistence &p);

// The app ended (returned, failed or was ended by the user): nothing to resume
void app_resume_ended(Persistence &p);

#define APP_RESUME_STARTED_MS 5000
