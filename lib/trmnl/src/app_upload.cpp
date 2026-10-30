#include "app_upload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <app_path.h>

app_upload_result_t app_upload_parse(const char *line, uint64_t fs_free, app_upload_header_t *h) {
  if (!line || !h) return APP_UPLOAD_BAD_HEADER;
  memset(h, 0, sizeof(*h));
  static const char put[] = "TAPP PUT ";
  if (strncmp(line, put, sizeof(put) - 1) != 0) return APP_UPLOAD_BAD_HEADER;
  const char *p = line + sizeof(put) - 1;
  const char *sp = strchr(p, ' ');
  if (!sp || sp == p || (size_t)(sp - p) + 7 > sizeof(h->path)) return APP_UPLOAD_BAD_NAME;
  char path[sizeof(h->path)];
  snprintf(path, sizeof(path), APPS_DIR "/%.*s", (int)(sp - p), p);
  char name[32];
  if (!app_path_name(path, name, sizeof(name))) return APP_UPLOAD_BAD_NAME;
  // <size> <crc>: decimal, then hex, nothing after
  p = sp + 1;
  if (*p < '0' || *p > '9') return APP_UPLOAD_BAD_HEADER;
  char *end;
  unsigned long long size = strtoull(p, &end, 10);
  if (*end != ' ' || end - p > 10) return APP_UPLOAD_BAD_HEADER;
  p = end + 1;
  if (!*p || strlen(p) > 8) return APP_UPLOAD_BAD_HEADER;
  for (const char *c = p; *c; c++) {
    bool hex = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F');
    if (!hex) return APP_UPLOAD_BAD_HEADER;
  }
  uint32_t crc = (uint32_t)strtoul(p, nullptr, 16);
  if (size == 0) return APP_UPLOAD_EMPTY;
  if (size > APP_UPLOAD_MAX) return APP_UPLOAD_TOO_BIG;
  if (fs_free < APP_UPLOAD_MARGIN || size > fs_free - APP_UPLOAD_MARGIN) return APP_UPLOAD_NO_SPACE;
  strcpy(h->path, path);
  h->size = (uint32_t)size;
  h->crc = crc;
  return APP_UPLOAD_OK;
}

const char *app_upload_find_header(const char *line) {
  return line ? strstr(line, "TAPP ") : nullptr;
}

void app_upload_got_text(const char *line, size_t max_bytes, char *out, size_t out_len) {
  if (!out || !out_len) return;
  size_t o = 0;
  out[0] = '\0';
  if (!line) return;
  o = (size_t)snprintf(out, out_len, "got");
  for (size_t i = 0; line[i] && i < max_bytes && o < out_len; i++)
    o += (size_t)snprintf(out + o, out_len - o, " %02x", (unsigned char)line[i]);
  if (o >= out_len) out[out_len - 1] = '\0';
}

app_upload_result_t app_upload_check(const app_upload_header_t *h, uint32_t got, uint32_t crc) {
  if (got != h->size) return APP_UPLOAD_SHORT;
  if (crc != h->crc) return APP_UPLOAD_BAD_CRC;
  return APP_UPLOAD_OK;
}

const char *app_upload_result_str(app_upload_result_t r) {
  switch (r) {
  case APP_UPLOAD_OK: return "ok";
  case APP_UPLOAD_BAD_HEADER: return "bad header";
  case APP_UPLOAD_BAD_NAME: return "bad name";
  case APP_UPLOAD_TOO_BIG: return "too big";
  case APP_UPLOAD_NO_SPACE: return "no space";
  case APP_UPLOAD_EMPTY: return "empty";
  case APP_UPLOAD_SHORT: return "short";
  case APP_UPLOAD_BAD_CRC: return "bad crc";
  case APP_UPLOAD_WRITE_FAILED: return "write failed";
  case APP_UPLOAD_REFUSED: return "refused";
  }
  return "?";
}
