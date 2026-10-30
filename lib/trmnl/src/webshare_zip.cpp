#include "webshare_zip.h"

#include <string.h>
#include <trmnl_crc32.h>

enum { ST_LOCAL, ST_DATA, ST_DESC, ST_CENTRAL, ST_EOCD, ST_DONE };

static void put16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
  put16(p, v);
  put16(p + 2, v >> 16);
}

bool ws_zip_begin(ws_zip_t *z, const ws_fs_t *fs, ws_zip_entry_t *entries, const char *const *paths, int count) {
  if (!z || !fs || !entries || count < 0 || count > WS_ZIP_MAX_FILES) return false;
  memset(z, 0, sizeof(*z));
  z->fs = fs;
  z->entries = entries;
  for (int i = 0; i < count; i++) {
    if (!paths[i] || !paths[i][0] || strlen(paths[i]) >= sizeof(entries[i].path)) return false;
    memset(&entries[i], 0, sizeof(entries[i]));
    strcpy(entries[i].path, paths[i]);
  }
  z->count = count;
  z->state = count ? ST_LOCAL : ST_EOCD;
  return true;
}

// Local header with flag 8: crc and sizes follow the data
static void stage_local(ws_zip_t *z) {
  ws_zip_entry_t *e = &z->entries[z->cur];
  size_t n = strlen(e->path);
  e->offset = z->out_pos;
  uint8_t *p = z->stage;
  memset(p, 0, 30);
  put32(p, 0x04034b50u);
  put16(p + 4, 20);
  put16(p + 6, 0x0808); // data descriptor, UTF-8 names
  put16(p + 12, 0x0021); // 1980-01-01
  put16(p + 26, (uint32_t)n);
  memcpy(p + 30, e->path, n);
  z->stage_len = 30 + n;
}

static void stage_desc(ws_zip_t *z) {
  ws_zip_entry_t *e = &z->entries[z->cur];
  e->crc = z->crc;
  e->size = z->size;
  put32(z->stage, 0x08074b50u);
  put32(z->stage + 4, e->crc);
  put32(z->stage + 8, e->size);
  put32(z->stage + 12, e->size);
  z->stage_len = 16;
}

static void stage_central(ws_zip_t *z) {
  ws_zip_entry_t *e = &z->entries[z->cur];
  size_t n = strlen(e->path);
  uint8_t *p = z->stage;
  memset(p, 0, 46);
  put32(p, 0x02014b50u);
  put16(p + 4, 20);
  put16(p + 6, 20);
  put16(p + 8, 0x0808);
  put16(p + 14, 0x0021);
  put32(p + 16, e->crc);
  put32(p + 20, e->size);
  put32(p + 24, e->size);
  put16(p + 28, (uint32_t)n);
  put32(p + 42, e->offset);
  memcpy(p + 46, e->path, n);
  z->stage_len = 46 + n;
}

static void stage_eocd(ws_zip_t *z) {
  uint8_t *p = z->stage;
  memset(p, 0, 22);
  put32(p, 0x06054b50u);
  put16(p + 8, (uint32_t)z->count);
  put16(p + 10, (uint32_t)z->count);
  put32(p + 12, z->cd_size);
  put32(p + 16, z->cd_start);
  z->stage_len = 22;
}

// After a staged block was written out: the next state
static void advance(ws_zip_t *z) {
  z->stage_len = z->stage_pos = 0;
  switch (z->state) {
  case ST_LOCAL:
    z->file = z->fs->open(z->fs->ctx, z->entries[z->cur].path, false);
    if (!z->file) {
      z->failed = true;
      return;
    }
    z->crc = 0;
    z->size = 0;
    z->state = ST_DATA;
    break;
  case ST_DESC:
    z->fs->close(z->fs->ctx, z->file);
    z->file = nullptr;
    if (++z->cur < z->count) {
      z->state = ST_LOCAL;
    } else {
      z->cur = 0;
      z->cd_start = z->out_pos;
      z->state = ST_CENTRAL;
    }
    break;
  case ST_CENTRAL:
    z->cd_size = z->out_pos - z->cd_start;
    if (++z->cur >= z->count) z->state = ST_EOCD;
    break;
  case ST_EOCD:
    z->state = ST_DONE;
    break;
  default:
    break;
  }
}

int ws_zip_read(ws_zip_t *z, void *buf, size_t cap) {
  uint8_t *out = (uint8_t *)buf;
  size_t n = 0;
  if (z->failed) return -1;
  while (n < cap && z->state != ST_DONE) {
    if (z->stage_pos < z->stage_len) { // drain a staged block
      size_t take = z->stage_len - z->stage_pos;
      if (take > cap - n) take = cap - n;
      memcpy(out + n, z->stage + z->stage_pos, take);
      z->stage_pos += take;
      n += take;
      z->out_pos += (uint32_t)take;
      if (z->stage_pos == z->stage_len) {
        // the central directory's size is only known after its last block: advance() reads out_pos
        advance(z);
        if (z->failed) return n ? (int)n : -1;
      }
      continue;
    }
    switch (z->state) { // stage the next block
    case ST_LOCAL:
      stage_local(z);
      break;
    case ST_DATA: {
      int got = z->fs->read(z->fs->ctx, z->file, out + n, cap - n);
      if (got < 0 || z->size > 0xFFFFFFFFu - (uint32_t)got) {
        z->failed = true;
        return n ? (int)n : -1;
      }
      if (got == 0) {
        z->state = ST_DESC;
        stage_desc(z);
        break;
      }
      z->crc = trmnl_crc32(z->crc, out + n, (size_t)got);
      z->size += (uint32_t)got;
      z->out_pos += (uint32_t)got;
      n += (size_t)got;
      break;
    }
    case ST_CENTRAL:
      stage_central(z);
      break;
    case ST_EOCD:
      stage_eocd(z);
      break;
    default:
      break;
    }
  }
  return (int)n;
}

void ws_zip_end(ws_zip_t *z) {
  if (z && z->file) z->fs->close(z->fs->ctx, z->file);
  if (z) z->file = nullptr;
}
