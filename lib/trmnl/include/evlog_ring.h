#pragma once

// The event log's ring (src/evlog.cpp keeps it on the X, docs/apps.md "Event log"), I/O free: recent device events
// (keyboards, apps, network) as short text lines with the uptime, the wall clock when set and a boot number. Its image
// (header + data, EVLOG_BYTES in all) is what goes to the flash file; a damaged image loads as an empty log.

#include <apps_config.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVLOG_MAGIC 0x31474C45u // "ELG1"
#define EVLOG_TEXT_MAX 100 // longer event text is cut
#define EVLOG_REC_HEAD 12 // per record: uptime ms, wall s, boot, repeat, text length
#define EVLOG_LINE_MAX (EVLOG_TEXT_MAX + 64) // a formatted line with its NUL fits

typedef struct {
  uint32_t magic; // EVLOG_MAGIC
  uint32_t cap; // data bytes
  uint32_t head; // offset of the oldest record
  uint32_t used; // bytes of records
  uint32_t last; // offset of the newest record (when used)
  uint32_t boot; // counted up at each start (evlog_ring_new_boot)
  uint32_t crc; // trmnl_crc32 over this header (crc 0) and all data
} evlog_hdr_t;

#define EVLOG_DATA_BYTES (EVLOG_BYTES - sizeof(evlog_hdr_t))

typedef struct {
  evlog_hdr_t h;
  uint8_t *data; // EVLOG_DATA_BYTES, the caller's
  bool dirty; // changed since the last flush
  bool flushed_once;
  uint32_t flushed_at;
} evlog_ring_t;

// Empty, boot 0, over the caller's `data` of EVLOG_DATA_BYTES
void evlog_ring_init(evlog_ring_t *r, uint8_t *data);

// From a saved image of EVLOG_BYTES; false (and the ring empty) when it is not one: wrong size, magic, CRC or records
bool evlog_ring_load(evlog_ring_t *r, const void *img, size_t len);

// The image into `out` (EVLOG_BYTES); returns its size, 0 when `cap` is too small
size_t evlog_ring_save(const evlog_ring_t *r, void *out, size_t cap);

// A new start: the boot number goes up (the records stay)
void evlog_ring_new_boot(evlog_ring_t *r);

// One event: `text` (control characters become spaces, cut at EVLOG_TEXT_MAX) at uptime `ms` and Unix `wall` (0 or a
// time before the clock counts as set: unknown). The same text as the newest record of this boot only counts up its
// repeat (to 255). The oldest records go when there is no room.
void evlog_ring_add(evlog_ring_t *r, uint32_t ms, int64_t wall, const char *text);

void evlog_ring_clear(evlog_ring_t *r); // no records; the boot number stays

int evlog_ring_count(const evlog_ring_t *r);

// Record `i` (0 the oldest) as "#<boot> <s>.<ms>s <YYYY-MM-DD HH:MM:SSZ or -> <text>[ (xN)]"; its length, -1 if none
int evlog_ring_line(const evlog_ring_t *r, int i, char *out, size_t cap);

// The last `n` records, oldest first, each ending in '\n', NUL-terminated in `buf`; fewer when they do not fit (the
// oldest of them go). Returns the lines written, -1 when buf is NULL or len 0
int evlog_ring_tail(const evlog_ring_t *r, uint32_t n, char *buf, size_t len);

// Flash writes only on events and at most every EVLOG_FLUSH_GAP_MS: true when a write is due at `now`
bool evlog_ring_flush_due(const evlog_ring_t *r, uint32_t now);
void evlog_ring_flushed(evlog_ring_t *r, uint32_t now);
