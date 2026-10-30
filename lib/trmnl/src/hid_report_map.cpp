#include "hid_report_map.h"

#include <string.h>

#define MAX_IDS    8
#define MAX_BITS   4096 // no keyboard report is this long: a larger offset means a broken map
#define MAX_USAGES 8
#define MAX_DEPTH  8

namespace {

struct ids_t {
  uint8_t id[MAX_IDS];
  uint32_t bits[MAX_IDS];
  int n;
};

// The bit counter of a report ID; nullptr when the table is full
uint32_t *bits_of(ids_t *t, uint8_t id) {
  for (int i = 0; i < t->n; i++) {
    if (t->id[i] == id) return &t->bits[i];
  }
  if (t->n == MAX_IDS) return nullptr;
  t->id[t->n] = id;
  t->bits[t->n] = 0;
  return &t->bits[t->n++];
}

struct locals_t {
  uint32_t usage[MAX_USAGES]; // page << 16 | usage, page 0: the global one
  int usages;
  uint32_t min, max;
  bool has_min, has_max;
};

} // namespace

static uint32_t item_value(const uint8_t *d, int size) {
  uint32_t v = 0;
  for (int i = 0; i < size; i++) v |= (uint32_t)d[i] << (8 * i);
  return v;
}

static int32_t item_signed(const uint8_t *d, int size) {
  uint32_t v = item_value(d, size);
  if (size == 1) return (int8_t)v;
  if (size == 2) return (int16_t)v;
  return (int32_t)v;
}

bool hid_map_parse(const uint8_t *map, size_t len, hid_map_kbd_t *out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
  out->mod_bit = out->keys_bit = out->bitmap_bit = -1;
  if (!map) return false;
  ids_t ids = {};
  locals_t loc = {};
  uint32_t page = 0, size = 0, count = 0;
  int32_t logical_min = 0;
  uint8_t report_id = 0;
  bool kbd_app[MAX_DEPTH] = {};
  int depth = 0;
  bool have_id = false; // the keyboard's report ID is chosen

  size_t i = 0;
  while (i < len) {
    uint8_t prefix = map[i++];
    if (prefix == 0xFE) { // long item: size, tag, data
      if (i + 2 > len) return false;
      i += 2 + map[i];
      continue;
    }
    int bsize = prefix & 3;
    if (bsize == 3) bsize = 4;
    if (i + (size_t)bsize > len) return false; // cut off
    const uint8_t *d = &map[i];
    i += bsize;
    uint32_t v = item_value(d, bsize);
    int type = (prefix >> 2) & 3, tag = prefix >> 4;
    if (type == 1) { // global
      switch (tag) {
      case 0: page = v; break;
      case 1: logical_min = item_signed(d, bsize); break;
      case 7: size = v; break;
      case 8:
        if (v == 0 || v > 255) return false;
        report_id = (uint8_t)v;
        break;
      case 9: count = v; break;
      default: break;
      }
      continue;
    }
    if (type == 2) { // local
      uint32_t u = bsize == 4 ? v : (page << 16 | v);
      if (tag == 0 && loc.usages < MAX_USAGES) loc.usage[loc.usages++] = u;
      if (tag == 1) loc.min = u, loc.has_min = true;
      if (tag == 2) loc.max = u, loc.has_max = true;
      continue;
    }
    if (type != 0) continue;
    // main item
    bool in_kbd = depth > 0 && kbd_app[depth - 1];
    if (tag == 0xA) { // collection
      if (depth == MAX_DEPTH) return false;
      bool app = bsize >= 1 && v == 1;
      bool kbd = app && loc.usages > 0 && loc.usage[0] == (0x01u << 16 | 0x06);
      kbd_app[depth] = in_kbd || kbd;
      depth++;
    } else if (tag == 0xC) { // end collection
      if (depth > 0) depth--;
    } else if (tag == 8) { // input
      if (size == 0 || size > 32 || count > MAX_BITS) return false;
      uint32_t *bits = bits_of(&ids, report_id);
      if (!bits) return false;
      uint32_t at = *bits;
      *bits += size * count;
      if (*bits > MAX_BITS) return false;
      bool constant = v & 1, variable = v & 2;
      bool keys_page = (loc.has_min ? loc.min >> 16 : loc.usages ? loc.usage[0] >> 16 : page) == 0x07;
      if (in_kbd && !constant && keys_page && (!have_id || report_id == out->report_id)) {
        uint32_t umin = loc.has_min ? loc.min & 0xFFFF : loc.usages ? loc.usage[0] & 0xFFFF : 0;
        uint32_t umax = loc.has_max ? loc.max & 0xFFFF : umin + count - 1;
        if (variable && size == 1) {
          if (umin == 0xE0 && umax == 0xE7 && count == 8) {
            if (out->mod_bit < 0) out->mod_bit = (int16_t)at;
          } else if (out->bitmap_bit < 0 && umin <= 0xFF) {
            out->bitmap_bit = (int16_t)at;
            out->bitmap_min = (uint8_t)umin;
            uint32_t n = umax >= umin ? umax - umin + 1 : 0;
            if (n > count) n = count;
            if (n > 255) n = 255;
            out->bitmap_count = (uint8_t)n;
          }
        } else if (!variable && size == 8 && out->keys_bit < 0) {
          out->keys_bit = (int16_t)at;
          out->keys_count = (uint8_t)(count > 255 ? 255 : count);
          out->keys_base = (int16_t)((int32_t)umin - logical_min);
        }
        if (out->mod_bit >= 0 || out->keys_bit >= 0 || out->bitmap_bit >= 0) {
          have_id = true;
          out->report_id = report_id;
        }
      }
    }
    if (tag == 8 || tag == 9 || tag == 0xA || tag == 0xB || tag == 0xC) memset(&loc, 0, sizeof(loc));
  }
  if (!have_id || (out->keys_bit < 0 && out->bitmap_bit < 0)) {
    out->found = false;
    return false;
  }
  uint32_t *bits = bits_of(&ids, out->report_id);
  out->report_bits = bits ? (uint16_t)*bits : 0;
  out->found = true;
  return true;
}

static uint32_t get_bits(const uint8_t *r, uint32_t bit, int n) {
  uint32_t v = 0;
  for (int k = 0; k < n; k++) {
    uint32_t b = bit + k;
    if (r[b / 8] & (1u << (b % 8))) v |= 1u << k;
  }
  return v;
}

bool hid_map_to_boot(const hid_map_kbd_t *k, const uint8_t *report, size_t len, uint8_t boot[8]) {
  if (!k || !k->found || !report || !boot) return false;
  if (len * 8 < k->report_bits) return false;
  memset(boot, 0, 8);
  if (k->mod_bit >= 0) boot[0] = (uint8_t)get_bits(report, (uint32_t)k->mod_bit, 8);
  int n = 0;
  bool rollover = false;
  if (k->keys_bit >= 0) {
    for (int i = 0; i < k->keys_count; i++) {
      int32_t u = (int32_t)get_bits(report, (uint32_t)k->keys_bit + 8u * i, 8);
      if (u == 0) continue;
      u += k->keys_base;
      if (u == 1) rollover = true;
      if (u < 4 || u > 0xFF) continue;
      if (u >= 0xE0 && u <= 0xE7) {
        boot[0] |= (uint8_t)(1u << (u - 0xE0));
        continue;
      }
      if (n == 6) {
        rollover = true;
        break;
      }
      boot[2 + n++] = (uint8_t)u;
    }
  }
  if (k->bitmap_bit >= 0) {
    for (int i = 0; i < k->bitmap_count; i++) {
      if (!get_bits(report, (uint32_t)k->bitmap_bit + i, 1)) continue;
      int u = k->bitmap_min + i;
      if (u >= 0xE0 && u <= 0xE7) {
        boot[0] |= (uint8_t)(1u << (u - 0xE0));
        continue;
      }
      if (u < 4 || u > 0xFF) continue;
      if (n == 6) {
        rollover = true;
        break;
      }
      boot[2 + n++] = (uint8_t)u;
    }
  }
  if (rollover) memset(boot + 2, 0x01, 6);
  return true;
}
