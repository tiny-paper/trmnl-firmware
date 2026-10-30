#include "webshare_names.h"

#include <drive_layout.h>

#include <stdio.h>
#include <string.h>

static bool ends_with(const char *s, size_t len, const char *suffix) {
  size_t n = strlen(suffix);
  if (len < n) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[len - n + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (c != suffix[i]) return false;
  }
  return true;
}

// The name rule is the USB drive's (drive_name_ok: one rule for every way files come in), plus the page's length
bool ws_name_ok(const char *name) {
  if (!name) return false;
  size_t len = strlen(name);
  return len <= WS_NAME_MAX && drive_name_ok(name, len);
}

// Splits "a/b/c" into parts; false if there are more than `max` or any is not a good name
static int split(const char *path, char parts[][WS_NAME_MAX + 1], int max) {
  int n = 0;
  const char *p = path;
  while (true) {
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (n == max || len > WS_NAME_MAX) return -1;
    memcpy(parts[n], p, len);
    parts[n][len] = '\0';
    if (!ws_name_ok(parts[n])) return -1;
    n++;
    if (!slash) break;
    p = slash + 1;
  }
  return n;
}

ws_kind_t ws_classify(const char *path) {
  if (!path || strlen(path) > WS_PATH_MAX) return WS_OTHER;
  char parts[3][WS_NAME_MAX + 1];
  int n = split(path, parts, 3);
  if (n == 3 && strcmp(parts[0], "documents") == 0) {
    size_t len = strlen(parts[2]);
    if ((ends_with(parts[2], len, ".md") || ends_with(parts[2], len, ".txt")) && len > 3 &&
        !(len == 4 && parts[2][0] == '.')) {
      return WS_DOC;
    }
  } else if (n == 3 && strcmp(parts[0], "settings") == 0 && strcmp(parts[1], "fonts") == 0) {
    size_t len = strlen(parts[2]);
    if (ends_with(parts[2], len, ".ttf") || ends_with(parts[2], len, ".otf")) return WS_FONT;
  }
  return WS_OTHER;
}

bool ws_alt_name(const char *path, int n, char *out, size_t out_len) {
  if (!out || n < 1 || n > 99 || ws_classify(path) == WS_OTHER) return false;
  const char *dot = strrchr(path, '.');
  const char *slash = strrchr(path, '/');
  size_t stem = (size_t)(dot - path);
  char suffix[16];
  int sl = snprintf(suffix, sizeof(suffix), " (web %d)", n);
  size_t ext = strlen(dot);
  size_t file_len = (size_t)(dot - (slash + 1)) + (size_t)sl + ext;
  if (file_len > WS_NAME_MAX || stem + (size_t)sl + ext > WS_PATH_MAX || stem + (size_t)sl + ext + 1 > out_len) {
    return false;
  }
  memcpy(out, path, stem);
  memcpy(out + stem, suffix, (size_t)sl);
  memcpy(out + stem + sl, dot, ext + 1);
  return true;
}
