#include "webshare_store.h"

#include <stdio.h>
#include <string.h>

#include "webshare_zip.h" // ws_crc32

const char *ws_result_str(ws_result_t r) {
  switch (r) {
  case WS_OK: return "ok";
  case WS_E_NAME: return "name not allowed";
  case WS_E_TOO_BIG: return "file too big";
  case WS_E_NO_SPACE: return "not enough space";
  case WS_E_BUSY: return "another upload is running";
  case WS_E_SHORT: return "upload incomplete";
  case WS_E_IO: return "write failed";
  case WS_E_NOT_FOUND: return "no such file";
  case WS_E_EXISTS: return "name already taken";
  case WS_E_KEEP_FAILED: return "name taken and too long to keep both";
  case WS_E_REFUSED: return "refused";
  }
  return "error";
}

static bool exists(const ws_fs_t *fs, const char *path, uint32_t *size = nullptr, bool *dir = nullptr) {
  uint32_t s = 0;
  bool d = false;
  bool ok = fs->stat(fs->ctx, path, &s, &d) == 0;
  if (size) *size = s;
  if (dir) *dir = d;
  return ok;
}

static void parent_of(const char *path, char *out, size_t len) {
  const char *slash = strrchr(path, '/');
  size_t n = slash ? (size_t)(slash - path) : 0;
  if (n >= len) n = len - 1;
  memcpy(out, path, n);
  out[n] = '\0';
}

static void tmp_in(const char *dir, char *out, size_t len) { snprintf(out, len, "%s/%s", dir, WS_TMP_NAME); }

static bool mkdirs(const ws_fs_t *fs, const char *dir) {
  char part[WS_PATH_MAX + 1];
  for (const char *p = dir; *p; p++) {
    if (p[1] != '/' && p[1] != '\0') continue;
    size_t n = (size_t)(p - dir) + 1;
    memcpy(part, dir, n);
    part[n] = '\0';
    bool is_dir = false;
    if (exists(fs, part, nullptr, &is_dir)) {
      if (!is_dir) return false;
    } else if (fs->mkdir(fs->ctx, part) < 0) {
      return false;
    }
  }
  return true;
}

typedef struct {
  const ws_fs_t *fs;
  char dirs[WS_ZIP_MAX_FILES][WS_NAME_MAX + 1];
  int n;
} dir_names_t;

static bool collect_dir(const char *name, bool is_dir, uint32_t, void *ctx) {
  dir_names_t *d = (dir_names_t *)ctx;
  if (is_dir && d->n < WS_ZIP_MAX_FILES && strlen(name) <= WS_NAME_MAX) strcpy(d->dirs[d->n++], name);
  return true;
}

static void remove_tmp_in(const ws_fs_t *fs, const char *dir) {
  char tmp[WS_PATH_MAX + 1];
  tmp_in(dir, tmp, sizeof(tmp));
  if (exists(fs, tmp)) fs->remove(fs->ctx, tmp);
}

void ws_store_cleanup(const ws_fs_t *fs) {
  static dir_names_t d; // large: not on the task's stack
  d.n = 0;
  fs->list(fs->ctx, "documents", collect_dir, &d);
  for (int i = 0; i < d.n; i++) {
    char dir[WS_PATH_MAX + 1];
    snprintf(dir, sizeof(dir), "documents/%s", d.dirs[i]);
    remove_tmp_in(fs, dir);
  }
  remove_tmp_in(fs, "settings/fonts");
}

ws_result_t ws_upload_begin_path(ws_upload_t *u, const ws_fs_t *fs, const char *path, uint32_t size,
                                 uint32_t max_size) {
  if (u->file) return WS_E_BUSY;
  if (size > max_size) return WS_E_TOO_BIG;
  uint64_t free_bytes = fs->free_bytes(fs->ctx);
  if (free_bytes < (uint64_t)size + WS_FREE_MARGIN) return WS_E_NO_SPACE;
  memset(u, 0, sizeof(*u));
  u->fs = fs;
  strcpy(u->path, path);
  char dir[WS_PATH_MAX + 1];
  parent_of(path, dir, sizeof(dir));
  if (!mkdirs(fs, dir)) return WS_E_IO;
  tmp_in(dir, u->tmp, sizeof(u->tmp));
  u->file = fs->open(fs->ctx, u->tmp, true);
  if (!u->file) return WS_E_IO;
  u->expected = size;
  return WS_OK;
}

ws_result_t ws_upload_begin(ws_upload_t *u, const ws_fs_t *fs, const char *path, uint32_t size) {
  if (u->file) return WS_E_BUSY;
  ws_kind_t kind = ws_classify(path);
  if (kind == WS_OTHER) return WS_E_NAME;
  return ws_upload_begin_path(u, fs, path, size, kind == WS_DOC ? WS_DOC_MAX : WS_FONT_MAX);
}

void ws_upload_abort(ws_upload_t *u) {
  if (!u || !u->fs) return;
  if (u->file) u->fs->close(u->fs->ctx, u->file);
  u->file = nullptr;
  if (exists(u->fs, u->tmp)) u->fs->remove(u->fs->ctx, u->tmp);
}

ws_result_t ws_upload_chunk(ws_upload_t *u, const void *data, size_t len) {
  if (!u->file) return WS_E_IO;
  if (len > u->expected - u->got) { // more than announced
    ws_upload_abort(u);
    return WS_E_SHORT;
  }
  if (len && u->fs->write(u->fs->ctx, u->file, data, len) != (int)len) {
    ws_upload_abort(u);
    return WS_E_NO_SPACE;
  }
  u->got += (uint32_t)len;
  u->crc = ws_crc32(u->crc, data, len);
  return WS_OK;
}

// CRC-32 of a stored file, or false if it cannot be read
static bool file_crc(const ws_fs_t *fs, const char *path, uint32_t *crc) {
  void *f = fs->open(fs->ctx, path, false);
  if (!f) return false;
  uint8_t buf[256];
  uint32_t c = 0;
  int n;
  while ((n = fs->read(fs->ctx, f, buf, sizeof(buf))) > 0) c = ws_crc32(c, buf, (size_t)n);
  fs->close(fs->ctx, f);
  *crc = c;
  return n == 0;
}

ws_result_t ws_upload_close(ws_upload_t *u, bool exact) {
  if (!u->file) return WS_E_IO;
  if (!exact) u->expected = u->got;
  if (u->got != u->expected) {
    ws_upload_abort(u);
    return WS_E_SHORT;
  }
  // Buffered bytes reach the disk at close: a failing close, or a file shorter than what was written (LittleFS
  // reports a full disk only there, and the Arduino wrapper drops the error), means the upload is lost
  int closed = u->fs->close(u->fs->ctx, u->file);
  u->file = nullptr;
  uint32_t on_disk = 0;
  if (closed < 0 || !exists(u->fs, u->tmp, &on_disk) || on_disk != u->got) {
    ws_upload_abort(u);
    return WS_E_NO_SPACE;
  }
  return WS_OK;
}

ws_result_t ws_upload_end(ws_upload_t *u, ws_put_t *how, char *final_path) {
  const ws_fs_t *fs = u->fs;
  ws_result_t closed = ws_upload_close(u, true);
  if (closed != WS_OK) return closed;
  char target[WS_PATH_MAX + 1];
  strcpy(target, u->path);
  *how = WS_PUT_NEW;
  uint32_t size = 0, crc = 0;
  if (exists(fs, target, &size)) {
    if (size == u->got && file_crc(fs, target, &crc) && crc == u->crc) {
      ws_upload_abort(u);
      *how = WS_PUT_SAME;
      strcpy(final_path, target);
      return WS_OK;
    }
    *how = WS_PUT_KEPT_BOTH;
    int n = 1;
    for (; n <= 99; n++) {
      if (!ws_alt_name(u->path, n, target, sizeof(target))) {
        ws_upload_abort(u);
        return WS_E_KEEP_FAILED;
      }
      if (!exists(fs, target)) break;
    }
    if (n > 99) {
      ws_upload_abort(u);
      return WS_E_KEEP_FAILED;
    }
  }
  // Checked just above; the writer app runs in another task, so the window is as small as it can be
  if (exists(fs, target) || fs->rename(fs->ctx, u->tmp, target) < 0) {
    ws_upload_abort(u);
    return WS_E_IO;
  }
  strcpy(final_path, target);
  return WS_OK;
}

ws_result_t ws_store_rename(const ws_fs_t *fs, const char *from, const char *to) {
  ws_kind_t kind = ws_classify(from);
  if (kind != WS_DOC || ws_classify(to) != WS_DOC) return WS_E_NAME;
  if (!exists(fs, from)) return WS_E_NOT_FOUND;
  if (exists(fs, to)) return WS_E_EXISTS;
  char dir[WS_PATH_MAX + 1];
  parent_of(to, dir, sizeof(dir));
  if (!mkdirs(fs, dir)) return WS_E_IO;
  return fs->rename(fs->ctx, from, to) < 0 ? WS_E_IO : WS_OK;
}

ws_result_t ws_store_delete(const ws_fs_t *fs, const char *path) {
  if (ws_classify(path) != WS_DOC) return WS_E_NAME; // fonts and everything else stay
  if (!exists(fs, path)) return WS_E_NOT_FOUND;
  return fs->remove(fs->ctx, path) < 0 ? WS_E_IO : WS_OK;
}

typedef struct {
  const ws_fs_t *fs;
  const char *dir;
  ws_kind_t want;
  int max, count;
  ws_files_cb_t cb;
  void *ctx;
  bool stop;
} walk_t;

static bool on_file(const char *name, bool is_dir, uint32_t size, void *ctx) {
  walk_t *w = (walk_t *)ctx;
  if (is_dir || w->count >= w->max) return !(w->count >= w->max);
  char path[WS_PATH_MAX + 1];
  if (snprintf(path, sizeof(path), "%s/%s", w->dir, name) >= (int)sizeof(path)) return true;
  if (ws_classify(path) != w->want) return true;
  w->count++;
  if (!w->cb(path, size, w->want, w->ctx)) {
    w->stop = true;
    return false;
  }
  return true;
}

int ws_store_list(const ws_fs_t *fs, bool fonts, int max, ws_files_cb_t cb, void *ctx) {
  walk_t w = {fs, "settings/fonts", WS_FONT, max, 0, cb, ctx, false};
  if (fonts) fs->list(fs->ctx, w.dir, on_file, &w);
  static dir_names_t d;
  d.n = 0;
  fs->list(fs->ctx, "documents", collect_dir, &d);
  w.want = WS_DOC;
  for (int i = 0; i < d.n && !w.stop && w.count < max; i++) {
    char dir[WS_PATH_MAX + 1];
    snprintf(dir, sizeof(dir), "documents/%s", d.dirs[i]);
    w.dir = dir;
    fs->list(fs->ctx, dir, on_file, &w);
  }
  return w.count;
}
