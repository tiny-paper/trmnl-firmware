#pragma once

// The writer's Wi-Fi page (app_api.h "Wi-Fi page", docs/apps.md): the firmware side. One at a time, for the app
// that is running (its folder is app_host.dir). The logic is in lib/trmnl/webshare_*.

#include <app_api.h>

int webshare_start(app_webshare_mode_t mode);
void webshare_stop(void); // safe when not running, from any task except the page's own
int webshare_start_files(app_webshare_mode_t mode, const app_webshare_files_t *files); // API 2.3, webshare_files.cpp
int webshare_start_settings(app_webshare_mode_t mode); // API 2.6, webshare_settings.cpp: the app's settings form
int webshare_status(app_webshare_status_t *st);

// The apps profile for the menu's "Install via Wi-Fi" (webshare_install.cpp): the same server and login, listing and
// accepting the ELF files in /apps instead of an app's documents. Joins the saved network or falls back to the access
// point; not part of the app API. webshare_status and webshare_stop work as for the writer page.
int webshare_start_install(void);

// The file system root as the page's file interface (for the app runner's .bak bookkeeping)
struct ws_fs;
const struct ws_fs *webshare_fs_root(void);
