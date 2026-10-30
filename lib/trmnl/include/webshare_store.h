#pragma once

#include "webshare_fs.h"
#include "webshare_names.h"

// What the Wi-Fi page does with the writer's files. Rules: nothing is ever overwritten; an upload goes to a
// temporary file and is renamed only when complete; a file that is not documents/ or settings/fonts/ is left alone.

#define WS_TMP_NAME ".webshare.tmp" // in the target's folder; leading dot, so the writer never lists it
#include <apps_config.h> // WS_DOC_MAX, WS_FONT_MAX, WS_FREE_MARGIN (left free after an upload)

typedef enum {
  WS_OK,
  WS_E_NAME, // not a document or font path
  WS_E_TOO_BIG, // over WS_DOC_MAX / WS_FONT_MAX
  WS_E_NO_SPACE, // would leave less than WS_FREE_MARGIN, or the disk filled up while writing
  WS_E_BUSY, // another upload is running
  WS_E_SHORT, // fewer or more bytes than announced (connection lost)
  WS_E_IO,
  WS_E_NOT_FOUND,
  WS_E_EXISTS, // rename target exists
  WS_E_KEEP_FAILED, // the name is taken and no " (web N)" name fits: refused, nothing written
  WS_E_REFUSED, // an app the ELF checker refused (the reason is given separately)
} ws_result_t;

typedef enum {
  WS_PUT_NEW, // stored under the requested name
  WS_PUT_SAME, // a file of that name with the same bytes exists: nothing written
  WS_PUT_KEPT_BOTH, // the name was taken by a different file: stored as `final_path`, the old one untouched
} ws_put_t;

typedef struct {
  const ws_fs_t *fs;
  void *file; // the temporary file, nullptr when idle
  char path[WS_PATH_MAX + 1];
  char tmp[WS_PATH_MAX + 1];
  uint32_t expected, got, crc;
} ws_upload_t;

const char *ws_result_str(ws_result_t r);

// Removes temporary files a power loss or a lost connection left behind (every folder of documents/ and
// settings/fonts/). Call when the page starts.
void ws_store_cleanup(const ws_fs_t *fs);

// Starts an upload of `size` bytes to `path`. Creates the folders. Checks name, size and free space first.
ws_result_t ws_upload_begin(ws_upload_t *u, const ws_fs_t *fs, const char *path, uint32_t size);
// The same for a path the caller has checked itself (apps/<name>.elf): only `max_size` and the free space apply
ws_result_t ws_upload_begin_path(ws_upload_t *u, const ws_fs_t *fs, const char *path, uint32_t size, uint32_t max_size);
// Closes the temporary file and checks what reached the disk: WS_OK, or the upload is dropped (WS_E_SHORT: not the
// announced `expected` bytes, unless `exact` is false, then whatever came; WS_E_NO_SPACE: lost at close)
ws_result_t ws_upload_close(ws_upload_t *u, bool exact);
ws_result_t ws_upload_chunk(ws_upload_t *u, const void *data, size_t len); // on an error the upload is already aborted
// Ends it: checks the byte count, then stores. `final_path` (WS_PATH_MAX + 1) is where the file went.
ws_result_t ws_upload_end(ws_upload_t *u, ws_put_t *how, char *final_path);
void ws_upload_abort(ws_upload_t *u); // connection lost: drops the temporary file

ws_result_t ws_store_rename(const ws_fs_t *fs, const char *from, const char *to);
ws_result_t ws_store_delete(const ws_fs_t *fs, const char *path);

// Lists documents (and fonts if `fonts`) as relative paths with sizes; skips everything else. Returns the count
// passed on; stops after `max`.
typedef bool (*ws_files_cb_t)(const char *path, uint32_t size, ws_kind_t kind, void *ctx);
int ws_store_list(const ws_fs_t *fs, bool fonts, int max, ws_files_cb_t cb, void *ctx);
