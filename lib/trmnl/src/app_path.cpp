#include "app_path.h"

#include <string.h>

#include <app_api.h>

static bool part_ok(const char *part, size_t len) {
  if (len == 0) return false;
  if (len == 1 && part[0] == '.') return false;
  if (len == 2 && part[0] == '.' && part[1] == '.') return false;
  return true;
}

bool app_path_join(const char *base, const char *rel, char *out, size_t out_len) {
  if (!base || !rel || !out) return false;
  size_t rel_len = strlen(rel);
  if (rel_len > APP_PATH_MAX || rel[0] == '/' || strchr(rel, '\\')) return false;
  const char *part = rel;
  while (rel_len && *part) {
    const char *slash = strchr(part, '/');
    size_t len = slash ? (size_t)(slash - part) : strlen(part);
    if (!part_ok(part, len)) return false;
    part += len + (slash ? 1 : 0);
    if (slash && !*part) return false; // trailing slash
  }
  size_t base_len = strlen(base);
  size_t need = base_len + (rel_len ? 1 + rel_len : 0) + 1;
  if (need > out_len) return false;
  memcpy(out, base, base_len);
  if (rel_len) {
    out[base_len] = '/';
    memcpy(out + base_len + 1, rel, rel_len);
  }
  out[need - 1] = '\0';
  return true;
}

bool app_path_name(const char *elf_path, char *name, size_t name_len) {
  static const char prefix[] = "/apps/";
  static const char suffix[] = ".elf";
  if (!elf_path || strncmp(elf_path, prefix, sizeof(prefix) - 1) != 0) return false;
  const char *start = elf_path + sizeof(prefix) - 1;
  size_t len = strlen(start);
  if (len <= sizeof(suffix) - 1 || strcmp(start + len - (sizeof(suffix) - 1), suffix) != 0) return false;
  len -= sizeof(suffix) - 1;
  if (len > 24 || len + 1 > name_len) return false;
  for (size_t i = 0; i < len; i++) {
    char c = start[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  memcpy(name, start, len);
  name[len] = '\0';
  return true;
}
