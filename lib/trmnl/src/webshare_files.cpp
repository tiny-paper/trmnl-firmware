#include "webshare_files.h"

#include <stdio.h>
#include <string.h>

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool dir_ok(const char *dir) {
  size_t len = strlen(dir);
  if (len > WS_FILES_DIR_MAX) return false;
  if (len == 0) return true;
  int parts = 0;
  for (const char *p = dir;;) {
    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    char part[WS_NAME_MAX + 1];
    if (++parts > 2 || n > WS_NAME_MAX) return false;
    memcpy(part, p, n);
    part[n] = '\0';
    if (!ws_name_ok(part)) return false;
    if (!slash) return true;
    p = slash + 1;
  }
}

bool ws_files_cfg_set(ws_files_cfg_t *cfg, const char *dir, const char *exts, uint32_t max_bytes) {
  if (!cfg || !dir || !exts || max_bytes == 0 || max_bytes > WS_FILES_BYTES_MAX || !dir_ok(dir)) return false;
  ws_files_cfg_t c;
  memset(&c, 0, sizeof(c));
  strcpy(c.dir, dir);
  c.max_bytes = max_bytes;
  for (const char *p = exts;;) {
    const char *comma = strchr(p, ',');
    size_t n = comma ? (size_t)(comma - p) : strlen(p);
    if (n < 2 || n > WS_FILES_EXT_LEN + 1 || p[0] != '.' || c.n_exts == WS_FILES_EXTS_MAX) return false;
    for (size_t i = 1; i < n; i++) {
      char ch = lower(p[i]);
      if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) return false;
      c.exts[c.n_exts][i] = ch;
    }
    c.exts[c.n_exts][0] = '.';
    c.n_exts++;
    if (!comma) break;
    p = comma + 1;
  }
  *cfg = c;
  return true;
}

bool ws_files_name_ok(const ws_files_cfg_t *cfg, const char *name) {
  if (!cfg || !ws_name_ok(name)) return false;
  const char *dot = strrchr(name, '.');
  if (!dot || dot == name) return false; // no stem
  for (int i = 0; i < cfg->n_exts; i++) {
    const char *e = cfg->exts[i];
    size_t n = strlen(e);
    if (strlen(dot) != n) continue;
    size_t k = 0;
    while (k < n && lower(dot[k]) == e[k]) k++;
    if (k == n) return true;
  }
  return false;
}

static void join(const ws_files_cfg_t *cfg, const char *name, char *out, size_t len) {
  if (cfg->dir[0]) snprintf(out, len, "%s/%s", cfg->dir, name);
  else snprintf(out, len, "%s", name);
}

void ws_files_why(const ws_files_cfg_t *cfg, ws_result_t rc, char *out, size_t out_len) {
  if (rc == WS_E_NAME) {
    size_t at = (size_t)snprintf(out, out_len, "name or type not allowed; only");
    for (int i = 0; i < cfg->n_exts && at < out_len; i++) at += (size_t)snprintf(out + at, out_len - at, " %s", cfg->exts[i]);
  } else if (rc == WS_E_TOO_BIG) {
    snprintf(out, out_len, "file too big (largest %u KB)", (unsigned)((cfg->max_bytes + 1023) / 1024));
  } else {
    snprintf(out, out_len, "%s", ws_result_str(rc));
  }
}

ws_result_t ws_files_begin(ws_upload_t *u, const ws_fs_t *fs, const ws_files_cfg_t *cfg, const char *name,
                           uint32_t size) {
  if (u->file) return WS_E_BUSY;
  if (!ws_files_name_ok(cfg, name)) return WS_E_NAME;
  char path[WS_PATH_MAX + 1];
  join(cfg, name, path, sizeof(path));
  return ws_upload_begin_path(u, fs, path, size, cfg->max_bytes);
}

static bool exists(const ws_fs_t *fs, const char *path) {
  uint32_t s;
  bool d;
  return fs->stat(fs->ctx, path, &s, &d) == 0;
}

ws_result_t ws_files_delete(const ws_fs_t *fs, const ws_files_cfg_t *cfg, const char *name) {
  if (!ws_files_name_ok(cfg, name)) return WS_E_NAME; // only what the page lists: never the temp file or a save
  char path[WS_PATH_MAX + 1];
  join(cfg, name, path, sizeof(path));
  uint32_t size;
  bool is_dir = false;
  if (fs->stat(fs->ctx, path, &size, &is_dir) != 0) return WS_E_NOT_FOUND;
  if (is_dir) return WS_E_NAME;
  return fs->remove(fs->ctx, path) < 0 ? WS_E_IO : WS_OK;
}

typedef struct {
  const ws_files_cfg_t *cfg;
  int max, count;
  ws_files_list_cb_t cb;
  void *ctx;
} list_t;

static bool on_entry(const char *name, bool is_dir, uint32_t size, void *ctx) {
  list_t *l = (list_t *)ctx;
  if (l->count >= l->max) return false;
  if (is_dir || !ws_files_name_ok(l->cfg, name)) return true;
  l->count++;
  return l->cb(name, size, l->ctx);
}

int ws_files_list(const ws_fs_t *fs, const ws_files_cfg_t *cfg, int max, ws_files_list_cb_t cb, void *ctx) {
  list_t l = {cfg, max, 0, cb, ctx};
  fs->list(fs->ctx, cfg->dir, on_entry, &l);
  return l.count;
}

void ws_files_cleanup(const ws_fs_t *fs, const ws_files_cfg_t *cfg) {
  char tmp[WS_PATH_MAX + 1];
  join(cfg, WS_TMP_NAME, tmp, sizeof(tmp));
  if (exists(fs, tmp)) fs->remove(fs->ctx, tmp);
}
