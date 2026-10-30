#pragma once

#include "webshare_fs.h"

// A zip of documents, written on the fly (stored, no compression, CRC-32 and sizes after the data), so a phone
// can download all documents without the X holding the zip in memory.

#define WS_ZIP_MAX_FILES 128

typedef struct {
  char path[65];
  uint32_t crc, size, offset;
} ws_zip_entry_t;

typedef struct {
  const ws_fs_t *fs;
  ws_zip_entry_t *entries; // caller's array of WS_ZIP_MAX_FILES
  int count, cur;
  int state;
  void *file;
  uint32_t out_pos, cd_start, cd_size, crc, size;
  uint8_t stage[160];
  size_t stage_len, stage_pos;
  bool failed;
} ws_zip_t;

// `paths`: relative paths of the files to include (count <= WS_ZIP_MAX_FILES, each WS_PATH_MAX at most)
bool ws_zip_begin(ws_zip_t *z, const ws_fs_t *fs, ws_zip_entry_t *entries, const char *const *paths, int count);
// Fills up to `cap` bytes. Returns the count, 0 at the end, < 0 if a file could not be read: the zip is then
// cut short and must not be offered as complete (the caller drops the connection).
int ws_zip_read(ws_zip_t *z, void *buf, size_t cap);
void ws_zip_end(ws_zip_t *z); // closes the file if one is open
