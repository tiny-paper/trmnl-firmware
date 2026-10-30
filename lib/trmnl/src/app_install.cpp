#include "app_install.h"

#include <stdio.h>
#include <string.h>

#include "app_path.h"
#include <app_elf_check.h>
#include <apps_config.h>

static void paths(const char *name, char *elf, char *bak, size_t len) {
  snprintf(elf, len, APPS_DIR_REL "/%s.elf", name);
  snprintf(bak, len, APPS_DIR_REL "/%s.elf.bak", name);
}

static bool exists(const ws_fs_t *fs, const char *path) {
  uint32_t size;
  bool dir;
  return fs->stat(fs->ctx, path, &size, &dir) == 0;
}

app_install_result_t app_install_commit(const ws_fs_t *fs, const char *tmp, const char *path, uint32_t size,
                                        uint8_t *buf, size_t buf_cap, size_t free_mem, char *reason,
                                        size_t reason_len) {
  if (reason_len) reason[0] = '\0';
  app_install_result_t rc = APP_INSTALL_OK;
  if (size > buf_cap || !buf) rc = APP_INSTALL_NO_SPACE;
  size_t have = 0;
  if (rc == APP_INSTALL_OK) {
    void *f = fs->open(fs->ctx, tmp, false);
    int n = 0;
    while (f && have < size && (n = fs->read(fs->ctx, f, buf + have, size - have)) > 0) have += (size_t)n;
    if (f) fs->close(fs->ctx, f);
    if (!f || have != size) rc = APP_INSTALL_IO;
  }
  if (rc == APP_INSTALL_OK) {
    app_info_t info;
    app_elf_result_t check = app_elf_check(buf, have, free_mem, &info);
    if (check != APP_ELF_OK) {
      snprintf(reason, reason_len, "%s", app_elf_result_str(check));
      rc = APP_INSTALL_REFUSED;
    }
  }
  char bak[80];
  snprintf(bak, sizeof(bak), "%s.bak", path);
  // The older version stays as .bak; if there already is one (an install not started yet) that one is the last
  // version known to work and stays
  if (rc == APP_INSTALL_OK && exists(fs, path) && !exists(fs, bak) && fs->rename(fs->ctx, path, bak) < 0) {
    rc = APP_INSTALL_IO;
  }
  if (rc == APP_INSTALL_OK && fs->rename(fs->ctx, tmp, path) < 0) {
    if (!exists(fs, path) && exists(fs, bak)) fs->rename(fs->ctx, bak, path); // put the old one back
    rc = APP_INSTALL_IO;
  }
  if (rc != APP_INSTALL_OK) fs->remove(fs->ctx, tmp);
  return rc;
}

bool app_install_name_ok(const char *file, char *name, size_t name_len) {
  if (!file || strlen(file) > 40) return false;
  char path[64];
  snprintf(path, sizeof(path), APPS_DIR "/%s", file);
  return app_path_name(path, name, name_len);
}

static bool restore(const ws_fs_t *fs, const char *name) {
  char elf[64], bak[64];
  paths(name, elf, bak, sizeof(elf));
  if (!exists(fs, bak)) return false;
  if (exists(fs, elf)) fs->remove(fs->ctx, elf);
  return fs->rename(fs->ctx, bak, elf) == 0;
}

void app_install_start(Persistence &p, const ws_fs_t *fs, const char *name) {
  char elf[64], bak[64];
  paths(name, elf, bak, sizeof(elf));
  if (exists(fs, bak)) p.writeString(APPI_STARTING_KEY, name);
}

void app_install_started(Persistence &p, const ws_fs_t *fs, const char *name) {
  char elf[64], bak[64];
  paths(name, elf, bak, sizeof(elf));
  if (p.readString(APPI_STARTING_KEY, "") == name) {
    fs->remove(fs->ctx, bak);
    p.remove(APPI_STARTING_KEY);
  }
}

bool app_install_failed(Persistence &p, const ws_fs_t *fs, const char *name) {
  p.remove(APPI_STARTING_KEY);
  bool back = restore(fs, name);
  if (back) p.writeString(APPI_NOTE_KEY, name);
  return back;
}

typedef struct {
  char bak[8][40]; // names of .elf.bak files, collected first: no renames while the directory is walked
  int n;
} baks_t;

static bool lost_elf_cb(const char *file, bool is_dir, uint32_t, void *ctx) {
  baks_t *b = (baks_t *)ctx;
  size_t n = strlen(file);
  if (!is_dir && n >= 9 && n < 40 && b->n < 8 && strcmp(file + n - 8, ".elf.bak") == 0) strcpy(b->bak[b->n++], file);
  return true;
}

void app_install_recover(Persistence &p, const ws_fs_t *fs, char *restored, size_t len) {
  if (len) restored[0] = '\0';
  baks_t baks;
  baks.n = 0;
  fs->list(fs->ctx, APPS_DIR_REL, lost_elf_cb, &baks);
  for (int i = 0; i < baks.n; i++) { // power went between the two renames of an install: no .elf, only the .bak
    char elf[64], bak[64];
    size_t n = strlen(baks.bak[i]);
    snprintf(bak, sizeof(bak), APPS_DIR_REL "/%s", baks.bak[i]);
    snprintf(elf, sizeof(elf), APPS_DIR_REL "/%.*s", (int)(n - 4), baks.bak[i]);
    if (!exists(fs, elf)) fs->rename(fs->ctx, bak, elf);
  }
  String starting = p.readString(APPI_STARTING_KEY, "");
  if (starting.length() && starting.length() < 32) {
    p.remove(APPI_STARTING_KEY);
    if (restore(fs, starting.c_str())) {
      p.writeString(APPI_NOTE_KEY, starting.c_str());
      if (len > starting.length()) strcpy(restored, starting.c_str());
    }
  }
}
