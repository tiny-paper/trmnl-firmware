#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The file system as the Wi-Fi page (webshare_*) sees it: the writer app's folder, paths relative to it.
// The firmware fills it from LittleFS (src/webshare.cpp); host tests fill it from memory and make it fail.
// Calls return 0 or a count, < 0 on failure (missing, disk full, I/O).

typedef bool (*ws_list_cb_t)(const char *name, bool is_dir, uint32_t size, void *ctx); // false stops

typedef struct ws_fs {
  void *ctx;
  int (*stat)(void *ctx, const char *path, uint32_t *size, bool *is_dir); // 0 if it exists
  void *(*open)(void *ctx, const char *path, bool write); // write creates or truncates; nullptr on failure
  int (*read)(void *ctx, void *file, void *buf, size_t len);
  int (*write)(void *ctx, void *file, const void *buf, size_t len); // len, or < 0 (disk full: short write)
  int (*close)(void *ctx, void *file);
  int (*rename)(void *ctx, const char *from, const char *to); // may replace: callers check first
  int (*remove)(void *ctx, const char *path);
  int (*mkdir)(void *ctx, const char *path);
  int (*list)(void *ctx, const char *dir, ws_list_cb_t cb, void *cb_ctx);
  uint64_t (*free_bytes)(void *ctx);
} ws_fs_t;
