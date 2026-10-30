#include "webshare_apps.h"

#include <stdio.h>
#include <string.h>

#include <app_elf_check.h>

#include "app_install.h"

ws_result_t ws_apps_begin(ws_upload_t *u, const ws_fs_t *fs, const char *file, uint32_t size) {
  char name[32], path[64];
  if (!app_install_name_ok(file, name, sizeof(name))) return WS_E_NAME;
  snprintf(path, sizeof(path), "apps/%s.elf", name);
  return ws_upload_begin_path(u, fs, path, size, WS_APP_MAX);
}

static bool exists(const ws_fs_t *fs, const char *path) {
  uint32_t s;
  bool d;
  return fs->stat(fs->ctx, path, &s, &d) == 0;
}

ws_result_t ws_apps_end(ws_upload_t *u, const ws_fs_t *fs, bool exact, uint8_t *buf, size_t buf_cap, size_t free_mem,
                        char *reason, size_t reason_len, char *installed, size_t installed_len) {
  reason[0] = '\0';
  ws_result_t rc = ws_upload_close(u, exact);
  if (rc != WS_OK) return rc;
  if (u->got > buf_cap) {
    ws_upload_abort(u);
    return WS_E_NO_SPACE;
  }
  void *f = fs->open(fs->ctx, u->tmp, false);
  size_t have = 0;
  int n = 0;
  while (f && have < u->got && (n = fs->read(fs->ctx, f, buf + have, u->got - have)) > 0) have += (size_t)n;
  if (f) fs->close(fs->ctx, f);
  if (!f || have != u->got) {
    ws_upload_abort(u);
    return WS_E_IO;
  }
  app_info_t info;
  app_elf_result_t check = app_elf_check(buf, have, free_mem, &info);
  if (check != APP_ELF_OK) {
    snprintf(reason, reason_len, "%s", app_elf_result_str(check));
    ws_upload_abort(u);
    return WS_E_REFUSED;
  }
  char bak[WS_PATH_MAX + 8];
  snprintf(bak, sizeof(bak), "%s.bak", u->path);
  // The older version stays as .bak; if there already is one (an install not started yet) that one is the last
  // version known to work and stays
  if (exists(fs, u->path)) {
    if (!exists(fs, bak) && fs->rename(fs->ctx, u->path, bak) < 0) {
      ws_upload_abort(u);
      return WS_E_IO;
    }
  }
  if (fs->rename(fs->ctx, u->tmp, u->path) < 0) {
    if (!exists(fs, u->path) && exists(fs, bak)) fs->rename(fs->ctx, bak, u->path); // put the old one back
    ws_upload_abort(u);
    return WS_E_IO;
  }
  snprintf(installed, installed_len, "%s", strrchr(u->path, '/') + 1);
  return WS_OK;
}

ws_result_t ws_apps_delete(const ws_fs_t *fs, const char *file) {
  char name[32], path[64], bak[72];
  if (!app_install_name_ok(file, name, sizeof(name))) return WS_E_NAME;
  snprintf(path, sizeof(path), "apps/%s.elf", name);
  snprintf(bak, sizeof(bak), "%s.bak", path);
  if (!exists(fs, path)) return WS_E_NOT_FOUND;
  if (fs->remove(fs->ctx, path) < 0) return WS_E_IO;
  if (exists(fs, bak)) fs->remove(fs->ctx, bak);
  return WS_OK;
}

typedef struct {
  const ws_fs_t *fs;
  int max, count;
  ws_apps_cb_t cb;
  void *ctx;
} list_t;

static bool on_entry(const char *file, bool is_dir, uint32_t size, void *ctx) {
  list_t *l = (list_t *)ctx;
  char name[32];
  if (is_dir || l->count >= l->max || !app_install_name_ok(file, name, sizeof(name))) return l->count < l->max;
  char path[64], bak[72];
  snprintf(path, sizeof(path), "apps/%s", file);
  snprintf(bak, sizeof(bak), "%s.bak", path);
  int64_t mtime = l->fs->mtime ? l->fs->mtime(l->fs->ctx, path) : 0;
  l->count++;
  return l->cb(file, size, mtime, exists(l->fs, bak), l->ctx);
}

int ws_apps_list(const ws_fs_t *fs, int max, ws_apps_cb_t cb, void *ctx) {
  list_t l = {fs, max, 0, cb, ctx};
  fs->list(fs->ctx, "apps", on_entry, &l);
  return l.count;
}

void ws_apps_cleanup(const ws_fs_t *fs) {
  if (exists(fs, "apps/" WS_TMP_NAME)) fs->remove(fs->ctx, "apps/" WS_TMP_NAME);
}
