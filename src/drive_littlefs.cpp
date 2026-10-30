// The drive's file operations on LittleFS, under the app's folder (drive_littlefs.h)
#ifdef BOARD_TRMNL_X

#include "drive_littlefs.h"

#include <LittleFS.h>
#include <app_api.h>
#include <app_path.h>

#define FULL_MAX (APP_PATH_MAX + 64)

static bool full(void *ctx, const char *rel, char *out) { return app_path_join((const char *)ctx, rel, out, FULL_MAX); }

static int64_t fs_size(void *ctx, const char *path) {
  char p[FULL_MAX];
  if (!full(ctx, path, p) || !LittleFS.exists(p)) return -1;
  File f = LittleFS.open(p, FILE_READ);
  if (!f || f.isDirectory()) return -1;
  int64_t n = (int64_t)f.size();
  f.close();
  return n;
}

static int fs_read(void *ctx, const char *path, uint32_t offset, uint8_t *buf, uint32_t len) {
  char p[FULL_MAX];
  if (!full(ctx, path, p) || !LittleFS.exists(p)) return DRIVE_EIO;
  File f = LittleFS.open(p, FILE_READ);
  if (!f || f.isDirectory() || !f.seek(offset)) return DRIVE_EIO;
  int n = (int)f.read(buf, len);
  f.close();
  return n < 0 ? DRIVE_EIO : n;
}

static int fs_write(void *ctx, const char *path, const uint8_t *buf, uint32_t len, bool append) {
  char p[FULL_MAX];
  if (!full(ctx, path, p)) return DRIVE_EINVAL;
  File f = LittleFS.open(p, append ? FILE_APPEND : FILE_WRITE);
  if (!f) return DRIVE_EIO;
  size_t n = len ? f.write(buf, len) : 0;
  f.close(); // LittleFS commits here
  return n == len ? 0 : DRIVE_EFULL;
}

static int fs_rename(void *ctx, const char *from, const char *to) {
  char a[FULL_MAX], b[FULL_MAX];
  if (!full(ctx, from, a) || !full(ctx, to, b)) return DRIVE_EINVAL;
  return LittleFS.rename(a, b) ? 0 : DRIVE_EIO;
}

static int fs_remove(void *ctx, const char *path) {
  char p[FULL_MAX];
  if (!full(ctx, path, p) || !LittleFS.exists(p)) return DRIVE_EIO;
  File f = LittleFS.open(p, FILE_READ);
  bool dir = f && f.isDirectory();
  f.close();
  return (dir ? LittleFS.rmdir(p) : LittleFS.remove(p)) ? 0 : DRIVE_EIO;
}

static int fs_mkdir(void *ctx, const char *path) {
  char p[FULL_MAX];
  if (!full(ctx, path, p)) return DRIVE_EINVAL;
  if (LittleFS.exists(p)) return 0;
  return LittleFS.mkdir(p) ? 0 : DRIVE_EIO;
}

static int fs_list(void *ctx, const char *dir, drive_list_fn cb, void *cbctx) {
  char p[FULL_MAX];
  if (!full(ctx, dir, p) || !LittleFS.exists(p)) return DRIVE_EIO;
  File root = LittleFS.open(p);
  if (!root || !root.isDirectory()) return DRIVE_EIO;
  // Names first: a callback may remove the entry (dropping the stage) or list a folder inside
  struct entry_t {
    char name[DRIVE_PATH_MAX + 1];
    bool dir;
    uint32_t size;
  };
  entry_t *entries = (entry_t *)malloc(DRIVE_MAX_FILES * sizeof(entry_t));
  if (!entries) {
    root.close();
    return DRIVE_ENOMEM;
  }
  int n = 0;
  while (File e = root.openNextFile()) {
    if (n < DRIVE_MAX_FILES && strlen(e.name()) <= DRIVE_PATH_MAX) {
      strcpy(entries[n].name, e.name());
      entries[n].dir = e.isDirectory();
      entries[n].size = (uint32_t)e.size();
      n++;
    }
    e.close();
  }
  root.close();
  for (int i = 0; i < n; i++) {
    if (!cb(cbctx, entries[i].name, entries[i].dir, entries[i].size)) break;
  }
  free(entries);
  return n;
}

static int64_t fs_free(void *ctx) {
  (void)ctx;
  return (int64_t)LittleFS.totalBytes() - (int64_t)LittleFS.usedBytes();
}

void drive_littlefs(drive_fs_t *fs, const char *base) {
  *fs = {(void *)base, fs_size, fs_read, fs_write, fs_rename, fs_remove, fs_mkdir, fs_list, fs_free};
}

#endif // BOARD_TRMNL_X
