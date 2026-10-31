#pragma once

#include "webshare_apps.h"
#include "webshare_store.h"

// The "files" profile of the Wi-Fi page: an app takes its own files (Game Boy ROMs, say) over Wi-Fi. The app names one
// folder inside its own app folder, the extensions it accepts and the largest file; the page lists, adds and deletes
// only those files. Same rules as the writer's uploads (webshare_store): temporary file, size and free-space checks,
// no silent overwrite (a different file of the same name is kept beside as "name (web N).ext").

#define WS_FILES_EXTS_MAX 8
#define WS_FILES_EXT_LEN 6 // letters and digits after the dot
#define WS_FILES_BYTES_MAX (32u * 1024u * 1024u)
#define WS_FILES_DIR_MAX 24 // bytes of the folder: room for a file name under WS_PATH_MAX

typedef struct {
  char dir[WS_FILES_DIR_MAX + 1]; // "" (the app folder), "roms" or "a/b"; relative to the app folder
  char exts[WS_FILES_EXTS_MAX][WS_FILES_EXT_LEN + 2]; // ".gb", lower case
  int n_exts;
  uint32_t max_bytes;
} ws_files_cfg_t;

// Takes what the app passed (`dir` and the comma separated `exts`, e.g. ".gb,.gbc") and checks it. False (nothing set)
// for a folder with an empty part, "." / "..", a leading slash or a bad name; no extension, a bad or more than
// WS_FILES_EXTS_MAX of them; `max_bytes` 0 or over WS_FILES_BYTES_MAX.
bool ws_files_cfg_set(ws_files_cfg_t *cfg, const char *dir, const char *exts, uint32_t max_bytes);

// A file name (no folder) the page accepts: a good name, a stem, one of the extensions (any case)
bool ws_files_name_ok(const ws_files_cfg_t *cfg, const char *name);

// The page's message for a refusal: names the extensions or the size, so the phone says what to do
void ws_files_why(const ws_files_cfg_t *cfg, ws_result_t rc, char *out, size_t out_len);

ws_result_t ws_files_begin(ws_upload_t *u, const ws_fs_t *fs, const ws_files_cfg_t *cfg, const char *name,
                           uint32_t size);
ws_result_t ws_files_delete(const ws_fs_t *fs, const ws_files_cfg_t *cfg, const char *name);

typedef bool (*ws_files_list_cb_t)(const char *name, uint32_t size, void *ctx);
int ws_files_list(const ws_fs_t *fs, const ws_files_cfg_t *cfg, int max, ws_files_list_cb_t cb, void *ctx);

// Removes a temporary file a power loss left in the folder
void ws_files_cleanup(const ws_fs_t *fs, const ws_files_cfg_t *cfg);
