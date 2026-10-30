#pragma once

#include <persistence_interface.h>

#include "webshare_store.h"

// The Wi-Fi page in apps mode ("Install via Wi-Fi"): upload, list and delete the ELF files in apps/ of the file
// system root. Same upload machinery as the writer page (webshare_store), plus the ELF checker before a file is kept.

#define WS_APP_MAX (2u * 1024u * 1024u)
#define WS_APPS_TMP_DIR "apps"

// Starts an upload of `file` ("<name>.elf") of up to `size` bytes (an upper bound is fine when the real size
// comes later: multipart). WS_E_NAME unless it is a valid app file name.
ws_result_t ws_apps_begin(ws_upload_t *u, const ws_fs_t *fs, const char *file, uint32_t size);

// Ends it: closes the temporary file, runs app_elf_check over it (`buf`, `buf_cap` bytes: at least the size of
// the upload; the caller allocates it, in PSRAM on the device; `free_mem`: what the loader could still allocate),
// and only then keeps it as apps/<name>.elf; an app of that name is kept as .bak (see app_install.h). `exact`:
// the byte count was announced and must match. A refused file is deleted and the checker's reason is in `reason`
// (WS_E_REFUSED). `installed` gets the file name.
ws_result_t ws_apps_end(ws_upload_t *u, const ws_fs_t *fs, bool exact, uint8_t *buf, size_t buf_cap, size_t free_mem,
                        char *reason, size_t reason_len, char *installed, size_t installed_len);

// Deletes apps/<name>.elf (and its .bak). The running app is not asked about: the page only runs from the menu
ws_result_t ws_apps_delete(const ws_fs_t *fs, const char *file);

typedef bool (*ws_apps_cb_t)(const char *file, uint32_t size, int64_t mtime, bool has_bak, void *ctx);
int ws_apps_list(const ws_fs_t *fs, int max, ws_apps_cb_t cb, void *ctx);

// Removes a temporary file a power loss left in apps/
void ws_apps_cleanup(const ws_fs_t *fs);
