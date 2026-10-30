#pragma once

#include <persistence_interface.h>

// Which app to resume after a restart (docs/apps.md), kept in NVS. An app is only resumed once it
// ran long enough to count as started (app_resume_started): starting it clears the flag, so an app
// that crashes while starting is not started again, and the device goes back to TRMNL.

#define APP_RESUME_PATH_KEY "app_last"
#define APP_RESUME_FLAG_KEY "app_resume"
#define APP_RESUME_INPUT_KEY "app_input" // a touch or key reached the app since it started

typedef enum {
  APP_RESUME_NONE, // nothing due (or a crash ended it)
  APP_RESUME_GO, // resume the app in `path`
  APP_RESUME_NO_INPUT, // due, but no input reached the app before the restart: not resumed, say so (the user may
                       // have had no way to leave it: a dead touch bar must not trap them in a resume loop)
} app_resume_t;

// The app to resume, if one is due: copies its path to `path` (also for APP_RESUME_NO_INPUT, to name it) and clears
// the flag (a crash before app_resume_started() then ends the resuming). `crashed`: the last reset was a panic or
// watchdog; nothing is resumed and the flag is cleared.
app_resume_t app_resume_take(Persistence &p, bool crashed, char *path, size_t path_len);

// A touch or key reached the running app (written once per run)
void app_resume_input(Persistence &p);

// An app is being started (from the menu or resumed): remembered, not yet due for resuming
void app_resume_starting(Persistence &p, const char *path);

// The app has run for APP_RESUME_STARTED_MS: resume it after a restart
void app_resume_started(Persistence &p);

// The app ended (returned, failed or was ended by the user): nothing to resume
void app_resume_ended(Persistence &p);

#define APP_RESUME_STARTED_MS 5000
