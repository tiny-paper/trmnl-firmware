#include "evlog_ring.h"

#include <app_clock.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <trmnl_crc32.h>

static_assert(sizeof(evlog_hdr_t) == 28, "the image header is part of the saved file");
static_assert(EVLOG_BYTES >= sizeof(evlog_hdr_t) + 4 * (EVLOG_REC_HEAD + EVLOG_TEXT_MAX),
              "event_log.bytes holds at least 4 full records");

typedef struct {
  uint32_t ms, wall;
  uint16_t boot;
  uint8_t repeat, len;
} rec_t;

static void get(const evlog_ring_t *r, uint32_t off, void *dst, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  for (uint32_t i = 0; i < n; i++) d[i] = r->data[(off + i) % r->h.cap];
}

static void put(evlog_ring_t *r, uint32_t off, const void *src, uint32_t n) {
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < n; i++) r->data[(off + i) % r->h.cap] = s[i];
}

static rec_t rec_at(const evlog_ring_t *r, uint32_t off) {
  uint8_t b[EVLOG_REC_HEAD];
  get(r, off, b, sizeof(b));
  rec_t x;
  x.ms = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
  x.wall = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
  x.boot = (uint16_t)(b[8] | b[9] << 8);
  x.repeat = b[10];
  x.len = b[11];
  return x;
}

static void rec_put(evlog_ring_t *r, uint32_t off, const rec_t &x) {
  uint8_t b[EVLOG_REC_HEAD] = {(uint8_t)x.ms, (uint8_t)(x.ms >> 8), (uint8_t)(x.ms >> 16), (uint8_t)(x.ms >> 24),
                               (uint8_t)x.wall, (uint8_t)(x.wall >> 8), (uint8_t)(x.wall >> 16), (uint8_t)(x.wall >> 24),
                               (uint8_t)x.boot, (uint8_t)(x.boot >> 8), x.repeat, x.len};
  put(r, off, b, sizeof(b));
}

static void empty(evlog_ring_t *r) {
  r->h.head = r->h.used = r->h.last = 0;
}

void evlog_ring_init(evlog_ring_t *r, uint8_t *data) {
  memset(r, 0, sizeof(*r));
  r->data = data;
  r->h.magic = EVLOG_MAGIC;
  r->h.cap = EVLOG_DATA_BYTES;
  memset(data, 0, EVLOG_DATA_BYTES);
}

static uint32_t image_crc(const evlog_hdr_t *h, const uint8_t *data) {
  evlog_hdr_t c = *h;
  c.crc = 0;
  return trmnl_crc32(trmnl_crc32(0, &c, sizeof(c)), data, EVLOG_DATA_BYTES);
}

// The records of `h` over `data` are whole and end exactly at `used`, the newest at `last`
static bool records_ok(const evlog_ring_t *r) {
  uint32_t off = r->h.head, left = r->h.used, newest = r->h.head;
  while (left) {
    if (left < EVLOG_REC_HEAD) return false;
    rec_t x = rec_at(r, off);
    uint32_t n = EVLOG_REC_HEAD + x.len;
    if (x.len > EVLOG_TEXT_MAX || x.repeat == 0 || n > left) return false;
    newest = off;
    off = (off + n) % r->h.cap;
    left -= n;
  }
  return r->h.used == 0 || newest == r->h.last;
}

bool evlog_ring_load(evlog_ring_t *r, const void *img, size_t len) {
  evlog_hdr_t h;
  const uint8_t *src = (const uint8_t *)img;
  bool ok = img && len == EVLOG_BYTES;
  if (ok) memcpy(&h, src, sizeof(h));
  ok = ok && h.magic == EVLOG_MAGIC && h.cap == EVLOG_DATA_BYTES && h.head < h.cap && h.used <= h.cap &&
       h.last < h.cap && h.crc == image_crc(&h, src + sizeof(h));
  if (ok) {
    memcpy(r->data, src + sizeof(h), EVLOG_DATA_BYTES);
    r->h = h;
    ok = records_ok(r);
  }
  if (!ok) {
    uint32_t boot = r->h.boot;
    evlog_ring_init(r, r->data);
    r->h.boot = boot;
  }
  r->dirty = false;
  return ok;
}

size_t evlog_ring_save(const evlog_ring_t *r, void *out, size_t cap) {
  if (!out || cap < EVLOG_BYTES) return 0;
  evlog_hdr_t h = r->h;
  h.crc = image_crc(&h, r->data);
  memcpy(out, &h, sizeof(h));
  memcpy((uint8_t *)out + sizeof(h), r->data, EVLOG_DATA_BYTES);
  return EVLOG_BYTES;
}

void evlog_ring_new_boot(evlog_ring_t *r) { r->h.boot = (uint16_t)(r->h.boot + 1); }

static bool same_text(const evlog_ring_t *r, uint32_t off, const char *text, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (r->data[(off + EVLOG_REC_HEAD + i) % r->h.cap] != (uint8_t)text[i]) return false;
  }
  return true;
}

void evlog_ring_add(evlog_ring_t *r, uint32_t ms, int64_t wall, const char *text) {
  evlog_ring_add_boot(r, (uint16_t)r->h.boot, ms, wall, text);
}

void evlog_ring_add_boot(evlog_ring_t *r, uint16_t boot, uint32_t ms, int64_t wall, const char *text) {
  char t[EVLOG_TEXT_MAX];
  uint8_t len = 0;
  for (const char *c = text ? text : ""; *c && len < EVLOG_TEXT_MAX; c++) {
    t[len++] = (unsigned char)*c < 0x20 || *c == 0x7F ? ' ' : *c;
  }
  rec_t x = {ms, wall >= APP_CLOCK_SET_AFTER && wall <= 0xFFFFFFFFll ? (uint32_t)wall : 0u, boot, 1,
             len};
  r->dirty = true;
  if (r->h.used) {
    rec_t newest = rec_at(r, r->h.last);
    if (newest.boot == x.boot && newest.len == len && newest.repeat < 255 && same_text(r, r->h.last, t, len)) {
      x.repeat = (uint8_t)(newest.repeat + 1);
      rec_put(r, r->h.last, x);
      return;
    }
  }
  uint32_t need = EVLOG_REC_HEAD + len;
  while (r->h.cap - r->h.used < need) { // the oldest go
    uint32_t n = EVLOG_REC_HEAD + rec_at(r, r->h.head).len;
    r->h.head = (r->h.head + n) % r->h.cap;
    r->h.used -= n;
  }
  if (!r->h.used) empty(r);
  uint32_t at = (r->h.head + r->h.used) % r->h.cap;
  rec_put(r, at, x);
  put(r, at + EVLOG_REC_HEAD, t, len);
  r->h.last = at;
  r->h.used += need;
}

void evlog_ring_clear(evlog_ring_t *r) {
  empty(r);
  r->dirty = true;
}

// Walks the records: `off` of the next one, `left` bytes after it
typedef struct {
  uint32_t off, left;
} walk_t;

static bool next(const evlog_ring_t *r, walk_t *w, uint32_t *at) {
  if (!w->left) return false;
  uint32_t n = EVLOG_REC_HEAD + rec_at(r, w->off).len;
  *at = w->off;
  w->off = (w->off + n) % r->h.cap;
  w->left -= n;
  return true;
}

int evlog_ring_count(const evlog_ring_t *r) {
  walk_t w = {r->h.head, r->h.used};
  uint32_t at;
  int n = 0;
  while (next(r, &w, &at)) n++;
  return n;
}

static int format(const evlog_ring_t *r, uint32_t at, char *out, size_t cap) {
  rec_t x = rec_at(r, at);
  char text[EVLOG_TEXT_MAX + 1], when[24] = "-";
  get(r, at + EVLOG_REC_HEAD, text, x.len);
  text[x.len] = '\0';
  if (x.wall) {
    time_t t = (time_t)x.wall;
    struct tm tm;
    if (gmtime_r(&t, &tm)) strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%SZ", &tm);
  }
  char rep[8] = "";
  if (x.repeat > 1) snprintf(rep, sizeof(rep), " (x%u)", (unsigned)x.repeat);
  return snprintf(out, cap, "#%u %lu.%03lus %s %s%s", (unsigned)x.boot, (unsigned long)(x.ms / 1000),
                  (unsigned long)(x.ms % 1000), when, text, rep);
}

int evlog_ring_line(const evlog_ring_t *r, int i, char *out, size_t cap) {
  walk_t w = {r->h.head, r->h.used};
  uint32_t at;
  for (int k = 0; next(r, &w, &at); k++) {
    if (k == i) return format(r, at, out, cap);
  }
  return -1;
}

int evlog_ring_tail(const evlog_ring_t *r, uint32_t n, char *buf, size_t len) {
  if (!buf || !len) return -1;
  buf[0] = '\0';
  int count = evlog_ring_count(r);
  int first = (uint32_t)count > n ? count - (int)n : 0;
  char line[EVLOG_LINE_MAX];
  // from the newest back: the lines that fit with their '\n' and the NUL
  size_t total = 1;
  int from = count;
  while (from > first) {
    int l = evlog_ring_line(r, from - 1, line, sizeof(line));
    if (l < 0 || total + (size_t)l + 1 > len) break;
    total += (size_t)l + 1;
    from--;
  }
  size_t pos = 0;
  for (int i = from; i < count; i++) {
    int l = evlog_ring_line(r, i, line, sizeof(line));
    memcpy(buf + pos, line, (size_t)l);
    pos += (size_t)l;
    buf[pos++] = '\n';
  }
  buf[pos] = '\0';
  return count - from;
}

bool evlog_ring_flush_due(const evlog_ring_t *r, uint32_t now) {
  return r->dirty && (!r->flushed_once || (uint32_t)(now - r->flushed_at) >= EVLOG_FLUSH_GAP_MS);
}

void evlog_ring_flushed(evlog_ring_t *r, uint32_t now) {
  r->dirty = false;
  r->flushed_once = true;
  r->flushed_at = now;
}
