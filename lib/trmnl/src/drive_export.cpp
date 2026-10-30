// The writer's layout files into the drive's FAT image (drive_sync.h)
#include <apps_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drive_sync.h"

// Two paths FAT cannot hold side by side: equal but for ASCII case in some part
static bool fat_clash(const char *a, const char *b) {
  bool part_case = false; // the current part differs only in case so far
  bool folder_case = false; // a folder of both differs only in case: FAT holds just one of them
  for (;; a++, b++) {
    char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
    char y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
    if (x != y) return folder_case;
    if (*a != *b) part_case = true;
    if (!*a) return true; // the same name on FAT
    if (*a == '/') {
      folder_case = folder_case || part_case;
      part_case = false;
    }
  }
}

typedef struct {
  const drive_fs_t *fs;
  drive_manifest_t *m;
  const char *dir; // being listed
  uint32_t not_shown;
  bool too_many;
} collect_t;

static void add(collect_t &c, const char *path) {
  if (!drive_layout_ok(path)) return;
  for (size_t i = 0; i < c.m->n; i++) {
    if (fat_clash(c.m->e[i].path, path)) {
      c.not_shown++;
      return;
    }
  }
  int64_t size = c.fs->size(c.fs->ctx, path);
  if (size < 0 || size > 0x7fffffff) return;
  if (c.m->n == DRIVE_MAX_FILES) {
    c.too_many = true;
    return;
  }
  drive_manifest_entry_t &e = c.m->e[c.m->n++];
  snprintf(e.path, sizeof(e.path), "%s", path);
  e.size = (uint32_t)size;
  e.hash = DRIVE_HASH_INIT;
}

static bool on_file(void *ctx, const char *name, bool is_dir, uint32_t size) {
  (void)size;
  collect_t &c = *(collect_t *)ctx;
  if (is_dir) return true;
  char path[DRIVE_PATH_MAX + 2];
  if ((size_t)snprintf(path, sizeof(path), "%s/%s", c.dir, name) >= sizeof(path)) return true;
  add(c, path);
  return !c.too_many;
}

static bool on_folder(void *ctx, const char *name, bool is_dir, uint32_t size) {
  (void)size;
  collect_t &c = *(collect_t *)ctx;
  if (!is_dir) return true;
  char dir[DRIVE_PATH_MAX + 2];
  if ((size_t)snprintf(dir, sizeof(dir), "documents/%s", name) >= sizeof(dir)) return true;
  const char *outer = c.dir;
  c.dir = dir;
  c.fs->list(c.fs->ctx, dir, on_file, &c);
  c.dir = outer;
  return !c.too_many;
}

typedef struct {
  const drive_fs_t *fs;
  drive_manifest_t *m;
} fill_t;

static bool fill(void *ctx, size_t index, uint8_t *dst, uint32_t len) {
  fill_t &f = *(fill_t *)ctx;
  drive_manifest_entry_t &e = f.m->e[index];
  uint32_t done = 0;
  while (done < len) {
    int n = f.fs->read(f.fs->ctx, e.path, done, dst + done, len - done);
    if (n <= 0) return false;
    done += (uint32_t)n;
  }
  e.hash = drive_hash(DRIVE_HASH_INIT, dst, len);
  return true;
}

int drive_export(const drive_fs_t *fs, uint8_t *img, size_t img_size, drive_manifest_t *m, uint32_t fat_time,
                 drive_export_result_t *res) {
  if (!fs || !img || !m || !m->e || !res) return DRIVE_EINVAL;
  m->n = 0;
  *res = {0, 0};
  collect_t c = {fs, m, "documents", 0, false};
  fs->list(fs->ctx, "documents", on_folder, &c);
  add(c, "settings/settings.json");
  c.dir = "settings/fonts";
  if (!c.too_many) fs->list(fs->ctx, "settings/fonts", on_file, &c);
  if (c.too_many) return DRIVE_ETOOMANY;

  drive_fat_file_t *files = (drive_fat_file_t *)calloc(m->n + 1, sizeof(drive_fat_file_t));
  if (!files) return DRIVE_ENOMEM;
  for (size_t i = 0; i < m->n; i++) files[i] = {m->e[i].path, m->e[i].size};
  fill_t f = {fs, m};
  int rc = drive_fat_build(img, img_size, DRIVE_VOLUME_LABEL, files, m->n, fill, &f, fat_time);
  free(files);
  if (rc == DRIVE_FAT_EFULL) return DRIVE_EFULL;
  if (rc == DRIVE_FAT_EINVAL) return DRIVE_EIO; // a read failed, or a name the checks above let through
  if (rc < 0) return DRIVE_EIO;
  res->shown = (uint32_t)m->n;
  res->not_shown = c.not_shown;
  return DRIVE_OK;
}
