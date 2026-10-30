#include "drive_layout.h"

#include <stdio.h>
#include <string.h>

// Length of the UTF-8 sequence at s (at most `left` bytes), 0 if invalid
static size_t utf8_len(const uint8_t *s, size_t left) {
  uint8_t c = s[0];
  size_t n;
  uint32_t min;
  if (c < 0x80) return 1;
  if ((c & 0xe0) == 0xc0) {
    n = 2;
    min = 0x80;
  } else if ((c & 0xf0) == 0xe0) {
    n = 3;
    min = 0x800;
  } else if ((c & 0xf8) == 0xf0) {
    n = 4;
    min = 0x10000;
  } else {
    return 0;
  }
  if (n > left) return 0;
  uint32_t cp = c & (0x7f >> n);
  for (size_t i = 1; i < n; i++) {
    if ((s[i] & 0xc0) != 0x80) return 0;
    cp = (cp << 6) | (s[i] & 0x3f);
  }
  if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
  return n;
}

bool drive_name_ok(const char *name, size_t len) {
  if (!name || len == 0 || name[0] == '.' || name[len - 1] == '.' || name[len - 1] == ' ') return false;
  const uint8_t *s = (const uint8_t *)name;
  for (size_t i = 0; i < len;) {
    uint8_t c = s[i];
    if (c < 0x20 || c == 0x7f || strchr("\\/:*?\"<>|", c)) return false;
    size_t n = utf8_len(s + i, len - i);
    if (n == 0) return false;
    i += n;
  }
  return true;
}

static bool ends_with(const char *s, size_t len, const char *suffix) {
  size_t n = strlen(suffix);
  return len > n && memcmp(s + len - n, suffix, n) == 0;
}

bool drive_layout_ok(const char *path) {
  if (!path) return false;
  size_t total = strlen(path);
  if (total == 0 || total > DRIVE_PATH_MAX) return false;
  const char *parts[4];
  size_t lens[4];
  int n = 0;
  const char *p = path;
  while (true) {
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (n == 4 || !drive_name_ok(p, len)) return false;
    parts[n] = p;
    lens[n] = len;
    n++;
    if (!slash) break;
    p = slash + 1;
  }
  if (n == 3 && lens[0] == 9 && memcmp(parts[0], "documents", 9) == 0) {
    return ends_with(parts[2], lens[2], ".md") || ends_with(parts[2], lens[2], ".txt");
  }
  if (n >= 2 && lens[0] == 8 && memcmp(parts[0], "settings", 8) == 0) {
    if (n == 2) return lens[1] == 13 && memcmp(parts[1], "settings.json", 13) == 0;
    return n == 3 && lens[1] == 5 && memcmp(parts[1], "fonts", 5) == 0;
  }
  return false;
}

bool drive_path_ok(const char *path) {
  if (!path || strlen(path) > DRIVE_PATH_MAX) return false;
  int n = 0;
  for (const char *p = path;;) {
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (++n > 3 || !drive_name_ok(p, len)) return false;
    if (!slash) return true;
    p = slash + 1;
  }
}

bool drive_conflict_name(const char *path, drive_exists_fn exists, void *ctx, char *out, size_t out_len) {
  if (!path || !out) return false;
  const char *slash = strrchr(path, '/');
  const char *base = slash ? slash + 1 : path;
  const char *dot = strrchr(base, '.');
  size_t stem = dot && dot != base ? (size_t)(dot - path) : strlen(path);
  const char *ext = path + stem;
  for (int i = 1; i < 100; i++) {
    char tag[16];
    if (i == 1) {
      snprintf(tag, sizeof(tag), " (computer)");
    } else {
      snprintf(tag, sizeof(tag), " (computer %d)", i);
    }
    size_t need = stem + strlen(tag) + strlen(ext);
    if (need > DRIVE_PATH_MAX || need + 1 > out_len) return false;
    memcpy(out, path, stem);
    snprintf(out + stem, out_len - stem, "%s%s", tag, ext);
    if (!exists || !exists(ctx, out)) return true;
  }
  return false;
}

uint64_t drive_hash(uint64_t h, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) h = (h ^ data[i]) * 0x100000001b3ull;
  return h;
}

bool drive_same_name_on_fat(const char *a, const char *b) {
  for (;; a++, b++) {
    char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
    char y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
    if (x != y) return false;
    if (!x) return true;
  }
}
