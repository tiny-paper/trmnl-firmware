#pragma once

#include <persistence_interface.h>

#include "webshare_fs.h"

// Installing an app over Wi-Fi (webshare_apps.h) keeps the app it replaces as apps/<name>.elf.bak until the new one
// has started once. This is that bookkeeping, around the app runner's start (docs/apps.md "Install via Wi-Fi"):
//
//   install      elf -> .bak (if the .bak is not there yet: an older, never started install keeps the first), new elf
//   start        the runner is about to load <name>: remembered as "starting" if there is a .bak
//   started      it ran (returned 0, or APP_RESUME_STARTED_MS passed): the .bak goes
//   failed       it did not load or returned an error: the .bak comes back
//   recover      at boot: an app still "starting" crashed its start (panic, watchdog): the .bak comes back; also an
//                .elf lost between the two renames of an install (power loss)
//
// The files are under apps/ of `fs` (the file system root).

#define APPI_STARTING_KEY "appi_start" // name of the app whose start is not over
#define APPI_NOTE_KEY "appi_note" // "<name>": the last start failed and its .bak was restored (for the page)

// The one way an app file becomes an installed app, for both USB serial (app_receive.cpp) and Install via Wi-Fi
// (webshare_apps.cpp): the complete temporary file `tmp` (`size` bytes, under the root of `fs`) is read into `buf`
// (`buf_cap` bytes, at least `size`), checked by app_elf_check (`free_mem`: what the loader could allocate), the
// app it replaces kept as <path>.bak (an existing .bak stays: it is the last version that started), then renamed to
// `path`. Refused or failed: the temp file is removed, the installed app untouched, `reason` says why (refusals).
typedef enum { APP_INSTALL_OK, APP_INSTALL_REFUSED, APP_INSTALL_NO_SPACE, APP_INSTALL_IO } app_install_result_t;
app_install_result_t app_install_commit(const ws_fs_t *fs, const char *tmp, const char *path, uint32_t size,
                                        uint8_t *buf, size_t buf_cap, size_t free_mem, char *reason,
                                        size_t reason_len);

// "<name>.elf" -> the name; false unless it is an app name (letters, digits, - and _, at most 24) with .elf
bool app_install_name_ok(const char *file, char *name, size_t name_len);

void app_install_start(Persistence &p, const ws_fs_t *fs, const char *name);
void app_install_started(Persistence &p, const ws_fs_t *fs, const char *name);
// true if an older version was restored
bool app_install_failed(Persistence &p, const ws_fs_t *fs, const char *name);
// Returns the name of an app that was restored, "" if none
void app_install_recover(Persistence &p, const ws_fs_t *fs, char *restored, size_t len);
