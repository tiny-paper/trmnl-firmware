#include "app_install.h"

#include <stdio.h>
#include <string.h>

#include "app_path.h"

static void paths(const char *name, char *elf, char *bak, size_t len) {
  snprintf(elf, len, "apps/%s.elf", name);
  snprintf(bak, len, "apps/%s.elf.bak", name);
}

static bool exists(const ws_fs_t *fs, const char *path) {
  uint32_t size;
  bool dir;
  return fs->stat(fs->ctx, path, &size, &dir) == 0;
}

bool app_install_name_ok(const char *file, char *name, size_t name_len) {
  if (!file || strlen(file) > 40) return false;
  char path[64];
  snprintf(path, sizeof(path), "/apps/%s", file);
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
  fs->list(fs->ctx, "apps", lost_elf_cb, &baks);
  for (int i = 0; i < baks.n; i++) { // power went between the two renames of an install: no .elf, only the .bak
    char elf[64], bak[64];
    size_t n = strlen(baks.bak[i]);
    snprintf(bak, sizeof(bak), "apps/%s", baks.bak[i]);
    snprintf(elf, sizeof(elf), "apps/%.*s", (int)(n - 4), baks.bak[i]);
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
