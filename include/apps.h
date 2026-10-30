#ifndef APPS_H
#define APPS_H

// Native apps on the TRMNL X (docs/apps.md): ELF files in /apps on the flash file system, started
// from the menu's Apps item. An app runs in its own task with the calls of app_api.h; a long press on
// the touch bar ends it. Afterwards the device goes back to TRMNL: the current image and sleep, or an
// error screen and sleep.

#include "apps_config.h" // APPS_DIR, APPS_DATA_DIR (config/apps.json)

#define APPS_MAX      8 // listed in the menu
#define APP_TZ_KEY    "app_tz" // NVS: the X's time zone for local_time, a POSIX TZ string; unset: UTC
#define APPS_PATH_MAX 40 // "/apps/<name>.elf", name up to 24 characters

// The apps in /apps, sorted by name: fills names (shown in the menu) and paths; returns how many
int apps_list(char names[][32], char paths[][APPS_PATH_MAX], int max);

// Receives apps over the USB serial port into /apps (app_upload.h, apps/upload.py) until `cancel`
// returns true or no upload starts for 2 minutes; returns how many were received
int apps_receive(bool (*cancel)(void));

// Loads and runs the app at `path`, then goes back to TRMNL. Does not return: it sleeps, or
// restarts when the app does not end within APP_EXIT_GRACE_MS of a long press.
void apps_run(const char *path);

// The menu's "Install via Wi-Fi": starts the Wi-Fi page in its apps profile, shows where it is, the code and a QR of
// the one-time login link, until a long press (or 15 min idle). Then back to TRMNL; does not return.
void apps_install_session(void);

// At a wake: runs the app that was running before a restart (apps_run, does not return), unless
// it did not get past its start, or the restart was a crash. Returns if there is none.
void apps_resume_if_due(void);

#endif // APPS_H
