#include "drive_fat.h"

#include <stdlib.h>
#include <string.h>

#include "drive_layout.h"

// FAT16, 512-byte sectors, one reserved sector, two FATs, a 512-entry root directory.
// Long names (VFAT) for every entry; the 8.3 alias is "TP" and the entry's index in hex.

#define SEC DRIVE_FAT_SECTOR
#define ROOT_ENTRIES 512
#define MIN_CLUSTERS 4085
#define MAX_CLUSTERS 65524
#define DIR_MAX_CLUSTERS 64
#define MAX_DEPTH 6
#define ATTR_VOLUME 0x08
#define ATTR_DIR 0x10
#define ATTR_LFN 0x0f
#define LFN_CHARS 13

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v) {
  wr16(p, v);
  wr16(p + 2, v >> 16);
}

typedef struct {
  uint32_t spc; // sectors per cluster
  uint32_t reserved, nfats, root_entries, fat_sectors, total;
  uint32_t root_start, root_sectors, data_start, clusters;
} geo_t;

static uint32_t csize(const geo_t &g) { return g.spc * SEC; }
static uint32_t cluster_offset(const geo_t &g, uint32_t c) { return (g.data_start + (c - 2) * g.spc) * SEC; }

static void geo_finish(geo_t &g) {
  g.root_start = g.reserved + g.nfats * g.fat_sectors;
  g.root_sectors = (g.root_entries * 32 + SEC - 1) / SEC;
  g.data_start = g.root_start + g.root_sectors;
  g.clusters = g.total > g.data_start ? (g.total - g.data_start) / g.spc : 0;
}

uint32_t drive_fat_time(int year, int month, int day, int hour, int minute, int second) {
  if (year < 1980) year = 1980;
  if (year > 2107) year = 2107;
  uint32_t date = (uint32_t)(year - 1980) << 9 | (uint32_t)(month & 15) << 5 | (uint32_t)(day & 31);
  uint32_t time = (uint32_t)(hour & 31) << 11 | (uint32_t)(minute & 63) << 5 | (uint32_t)((second / 2) & 31);
  return date << 16 | time;
}

// ---- names ----

// UTF-8 to UTF-16 units; returns the count, or -1 if invalid or longer than max
static int to_utf16(const char *s, size_t len, uint16_t *out, int max) {
  int n = 0;
  for (size_t i = 0; i < len;) {
    uint8_t c = (uint8_t)s[i];
    uint32_t cp;
    size_t k;
    if (c < 0x80) {
      cp = c;
      k = 1;
    } else if ((c & 0xe0) == 0xc0) {
      cp = c & 0x1f;
      k = 2;
    } else if ((c & 0xf0) == 0xe0) {
      cp = c & 0x0f;
      k = 3;
    } else if ((c & 0xf8) == 0xf0) {
      cp = c & 0x07;
      k = 4;
    } else {
      return -1;
    }
    if (i + k > len) return -1;
    for (size_t j = 1; j < k; j++) cp = cp << 6 | ((uint8_t)s[i + j] & 0x3f);
    i += k;
    if (cp >= 0x10000) {
      if (n + 2 > max) return -1;
      cp -= 0x10000;
      out[n++] = (uint16_t)(0xd800 | cp >> 10);
      out[n++] = (uint16_t)(0xdc00 | (cp & 0x3ff));
    } else {
      if (n + 1 > max) return -1;
      out[n++] = (uint16_t)cp;
    }
  }
  return n;
}

// Appends UTF-8 of cp to out (bounded); false if it does not fit
static bool put_utf8(char *out, size_t &pos, size_t cap, uint32_t cp) {
  char b[4];
  size_t k;
  if (cp < 0x80) {
    b[0] = (char)cp;
    k = 1;
  } else if (cp < 0x800) {
    b[0] = (char)(0xc0 | cp >> 6);
    b[1] = (char)(0x80 | (cp & 0x3f));
    k = 2;
  } else if (cp < 0x10000) {
    b[0] = (char)(0xe0 | cp >> 12);
    b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    b[2] = (char)(0x80 | (cp & 0x3f));
    k = 3;
  } else {
    b[0] = (char)(0xf0 | cp >> 18);
    b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    b[3] = (char)(0x80 | (cp & 0x3f));
    k = 4;
  }
  if (pos + k >= cap) return false;
  memcpy(out + pos, b, k);
  pos += k;
  return true;
}

static uint8_t lfn_checksum(const uint8_t *short_name) {
  uint8_t sum = 0;
  for (int i = 0; i < 11; i++) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + short_name[i]);
  return sum;
}

static const uint8_t lfn_offsets[LFN_CHARS] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

// ---- building ----

typedef struct {
  int parent; // -1 for the root
  const char *name; // into a file's path
  size_t name_len;
  uint32_t entries; // 32-byte entries it holds
  uint32_t first, count; // clusters
} bdir_t;

static int lfn_entries(const char *name, size_t len) {
  uint16_t units[260];
  int n = to_utf16(name, len, units, 255);
  return n <= 0 ? -1 : (n + LFN_CHARS - 1) / LFN_CHARS;
}

// A name FAT can hold: drive_name_ok, but hidden names (".Trashes", "._x.md") too
static bool fat_name_ok(const char *name, size_t len) {
  while (len && name[0] == '.') {
    name++;
    len--;
  }
  return drive_name_ok(name, len);
}

static int find_dir(const bdir_t *dirs, int ndirs, int parent, const char *name, size_t len) {
  for (int i = 1; i < ndirs; i++) {
    if (dirs[i].parent == parent && dirs[i].name_len == len && memcmp(dirs[i].name, name, len) == 0) return i;
  }
  return -1;
}

// Writes the long-name entries and the short entry for `name` at e (enough room checked before)
static uint8_t *put_entry(uint8_t *e, const char *name, size_t len, uint32_t index, uint8_t attr, uint32_t first,
                          uint32_t size, uint32_t fat_time) {
  uint8_t sn[11];
  memset(sn, ' ', sizeof(sn));
  static const char hex[] = "0123456789ABCDEF";
  sn[0] = 'T';
  sn[1] = 'P';
  for (int i = 0; i < 6; i++) sn[2 + i] = (uint8_t)hex[(index >> (20 - 4 * i)) & 15];
  const char *dot = attr == ATTR_DIR ? nullptr : strrchr(name, '.'); // a file's name ends the path
  if (dot) {
    for (int i = 0; i < 3 && dot + 1 + i < name + len; i++) {
      char c = dot[1 + i];
      if (c >= 'a' && c <= 'z') c = (char)(c - 32);
      bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
      sn[8 + i] = (uint8_t)(ok ? c : '_');
    }
  }
  uint16_t units[260];
  int n = to_utf16(name, len, units, 255);
  int count = (n + LFN_CHARS - 1) / LFN_CHARS;
  uint8_t sum = lfn_checksum(sn);
  for (int k = count; k >= 1; k--) {
    memset(e, 0, 32);
    e[0] = (uint8_t)(k | (k == count ? 0x40 : 0));
    e[11] = ATTR_LFN;
    e[13] = sum;
    for (int j = 0; j < LFN_CHARS; j++) {
      int u = (k - 1) * LFN_CHARS + j;
      uint16_t v = u < n ? units[u] : u == n ? 0x0000 : 0xffff;
      wr16(e + lfn_offsets[j], v);
    }
    e += 32;
  }
  memset(e, 0, 32);
  memcpy(e, sn, 11);
  e[11] = attr;
  wr16(e + 14, fat_time);
  wr16(e + 16, fat_time >> 16);
  wr16(e + 18, fat_time >> 16);
  wr16(e + 22, fat_time);
  wr16(e + 24, fat_time >> 16);
  wr16(e + 26, first);
  wr32(e + 28, size);
  return e + 32;
}

static void put_dot(uint8_t *e, const char *dots, uint32_t cluster, uint32_t fat_time) {
  memset(e, 0, 32);
  memset(e, ' ', 11);
  memcpy(e, dots, strlen(dots));
  e[11] = ATTR_DIR;
  wr16(e + 22, fat_time);
  wr16(e + 24, fat_time >> 16);
  wr16(e + 26, cluster);
}

// A contiguous chain of `count` clusters from `first`
static void put_chain(uint8_t *fat, uint32_t first, uint32_t count) {
  for (uint32_t c = first; count && c < first + count; c++) wr16(fat + c * 2, c == first + count - 1 ? 0xffff : c + 1);
}

static void write_boot(uint8_t *img, const geo_t &g, const char *label) {
  uint8_t *b = img;
  memset(b, 0, SEC);
  b[0] = 0xeb;
  b[1] = 0x3c;
  b[2] = 0x90;
  memcpy(b + 3, "MSDOS5.0", 8);
  wr16(b + 11, SEC);
  b[13] = (uint8_t)g.spc;
  wr16(b + 14, g.reserved);
  b[16] = (uint8_t)g.nfats;
  wr16(b + 17, g.root_entries);
  if (g.total < 65536) {
    wr16(b + 19, g.total);
  } else {
    wr32(b + 32, g.total);
  }
  b[21] = 0xf8;
  wr16(b + 22, g.fat_sectors);
  wr16(b + 24, 63);
  wr16(b + 26, 255);
  b[36] = 0x80;
  b[38] = 0x29;
  wr32(b + 39, 0x54504452); // volume id
  memset(b + 43, ' ', 11);
  memcpy(b + 43, label, strnlen(label, 11));
  memcpy(b + 54, "FAT16   ", 8);
  b[510] = 0x55;
  b[511] = 0xaa;
}

int drive_fat_build(uint8_t *img, size_t img_size, const char *label, const drive_fat_file_t *files, size_t n,
                    drive_fat_fill_fn fill, void *ctx, uint32_t fat_time) {
  if (!img || !label || (n && (!files || !fill)) || img_size % SEC || img_size < DRIVE_FAT_MIN_BYTES ||
      img_size / SEC > 0xffffffffu) {
    return DRIVE_FAT_EINVAL;
  }
  geo_t g = {};
  g.total = (uint32_t)(img_size / SEC);
  g.reserved = 1;
  g.nfats = 2;
  g.root_entries = ROOT_ENTRIES;
  for (g.spc = 1; g.spc <= 128; g.spc *= 2) {
    g.fat_sectors = 1;
    for (;;) {
      geo_finish(g);
      uint32_t need = ((g.clusters + 2) * 2 + SEC - 1) / SEC;
      if (need <= g.fat_sectors) break;
      g.fat_sectors = need;
    }
    if (g.clusters <= MAX_CLUSTERS) break;
  }
  if (g.spc > 128 || g.clusters < MIN_CLUSTERS) return DRIVE_FAT_EINVAL;

  // The folder tree: every folder of every path, then the entries each holds
  bdir_t *dirs = (bdir_t *)calloc(n * 3 + 1, sizeof(bdir_t));
  int *file_dir = (int *)calloc(n + 1, sizeof(int));
  int rc = DRIVE_FAT_OK;
  if (!dirs || !file_dir) rc = DRIVE_FAT_EINVAL;
  int ndirs = 1;
  if (rc == DRIVE_FAT_OK) {
    dirs[0].parent = -1;
    dirs[0].entries = 1; // the volume label
  }
  for (size_t i = 0; rc == DRIVE_FAT_OK && i < n; i++) {
    const char *p = files[i].path;
    int parent = 0;
    int depth = 0;
    for (;;) {
      const char *slash = strchr(p, '/');
      size_t len = slash ? (size_t)(slash - p) : strlen(p);
      if (!fat_name_ok(p, len) || lfn_entries(p, len) < 0 || ++depth > MAX_DEPTH) {
        rc = DRIVE_FAT_EINVAL;
        break;
      }
      if (!slash) {
        file_dir[i] = parent;
        dirs[parent].entries += (uint32_t)lfn_entries(p, len) + 1;
        break;
      }
      int d = find_dir(dirs, ndirs, parent, p, len);
      if (d < 0) {
        if (ndirs == (int)(n * 3 + 1)) {
          rc = DRIVE_FAT_EINVAL;
          break;
        }
        d = ndirs++;
        dirs[d] = {parent, p, len, 2, 0, 0};
        dirs[parent].entries += (uint32_t)lfn_entries(p, len) + 1;
      }
      parent = d;
      p = slash + 1;
    }
  }
  // Two names in one folder that FAT takes for the same (case), or a file named like a folder
  for (size_t i = 0; rc == DRIVE_FAT_OK && i < n; i++) {
    for (size_t j = i + 1; j < n; j++) {
      if (drive_same_name_on_fat(files[i].path, files[j].path)) rc = DRIVE_FAT_EINVAL;
    }
    const char *base = strrchr(files[i].path, '/');
    base = base ? base + 1 : files[i].path;
    size_t blen = strlen(base);
    for (int d = 1; d < ndirs; d++) {
      if (dirs[d].parent == file_dir[i] && dirs[d].name_len == blen && strncmp(dirs[d].name, base, blen) == 0) {
        rc = DRIVE_FAT_EINVAL;
      }
    }
  }
  if (rc == DRIVE_FAT_OK && dirs[0].entries > ROOT_ENTRIES) rc = DRIVE_FAT_EFULL;

  // Clusters, in order: folders, then files
  uint32_t next = 2;
  const uint32_t end = g.clusters + 2;
  uint32_t *file_first = rc == DRIVE_FAT_OK ? (uint32_t *)calloc(n + 1, sizeof(uint32_t)) : nullptr;
  if (rc == DRIVE_FAT_OK && !file_first) rc = DRIVE_FAT_EINVAL;
  for (int d = 1; rc == DRIVE_FAT_OK && d < ndirs; d++) {
    dirs[d].count = (dirs[d].entries * 32 + csize(g) - 1) / csize(g);
    if (dirs[d].count > DIR_MAX_CLUSTERS || dirs[d].count > end - next) {
      rc = DRIVE_FAT_EFULL;
      break;
    }
    dirs[d].first = next;
    next += dirs[d].count;
  }
  for (size_t i = 0; rc == DRIVE_FAT_OK && i < n; i++) {
    uint32_t count = (uint32_t)(((uint64_t)files[i].size + csize(g) - 1) / csize(g));
    if (count > end - next) {
      rc = DRIVE_FAT_EFULL;
      break;
    }
    file_first[i] = count ? next : 0;
    next += count;
  }

  if (rc == DRIVE_FAT_OK) {
    memset(img, 0, (size_t)g.data_start * SEC);
    write_boot(img, g, label);
    uint8_t *fat = img + g.reserved * SEC;
    wr16(fat, 0xfff8);
    wr16(fat + 2, 0xffff);
    for (int d = 1; d < ndirs; d++) put_chain(fat, dirs[d].first, dirs[d].count);
    for (size_t i = 0; i < n; i++) {
      put_chain(fat, file_first[i], (uint32_t)(((uint64_t)files[i].size + csize(g) - 1) / csize(g)));
    }
    memcpy(fat + g.fat_sectors * SEC, fat, (size_t)g.fat_sectors * SEC);
    for (int d = 1; d < ndirs; d++) memset(img + cluster_offset(g, dirs[d].first), 0, (size_t)dirs[d].count * csize(g));

    // Entries: a cursor per folder
    uint8_t **cur = (uint8_t **)calloc((size_t)ndirs, sizeof(uint8_t *));
    uint32_t *index = (uint32_t *)calloc((size_t)ndirs, sizeof(uint32_t));
    if (!cur || !index) rc = DRIVE_FAT_EINVAL;
    for (int d = 0; rc == DRIVE_FAT_OK && d < ndirs; d++) {
      if (d == 0) {
        cur[0] = img + g.root_start * SEC;
        memset(cur[0], ' ', 11);
        memcpy(cur[0], label, strnlen(label, 11));
        cur[0][11] = ATTR_VOLUME;
        wr16(cur[0] + 22, fat_time);
        wr16(cur[0] + 24, fat_time >> 16);
        cur[0] += 32;
        continue;
      }
      cur[d] = img + cluster_offset(g, dirs[d].first);
      put_dot(cur[d], ".", dirs[d].first, fat_time);
      put_dot(cur[d] + 32, "..", dirs[d].parent ? dirs[dirs[d].parent].first : 0, fat_time);
      cur[d] += 64;
    }
    for (int d = 1; rc == DRIVE_FAT_OK && d < ndirs; d++) {
      int p = dirs[d].parent;
      cur[p] = put_entry(cur[p], dirs[d].name, dirs[d].name_len, index[p]++, ATTR_DIR, dirs[d].first, 0, fat_time);
    }
    for (size_t i = 0; rc == DRIVE_FAT_OK && i < n; i++) {
      int p = file_dir[i];
      const char *base = strrchr(files[i].path, '/');
      base = base ? base + 1 : files[i].path;
      cur[p] = put_entry(cur[p], base, strlen(base), index[p]++, 0x20, file_first[i], files[i].size, fat_time);
      if (files[i].size && !fill(ctx, i, img + cluster_offset(g, file_first[i]), files[i].size)) rc = DRIVE_FAT_EINVAL;
    }
    free(cur);
    free(index);
  }
  free(file_first);
  free(dirs);
  free(file_dir);
  return rc;
}

// ---- reading ----

static int parse_geo(const uint8_t *img, size_t img_size, geo_t &g) {
  if (img_size < SEC || img_size % SEC) return DRIVE_FAT_EINVAL;
  const uint8_t *b = img;
  if (b[510] != 0x55 || b[511] != 0xaa || rd16(b + 11) != SEC) return DRIVE_FAT_EFORMAT;
  g.spc = b[13];
  g.reserved = rd16(b + 14);
  g.nfats = b[16];
  g.root_entries = rd16(b + 17);
  g.total = rd16(b + 19) ? rd16(b + 19) : rd32(b + 32);
  g.fat_sectors = rd16(b + 22);
  if (g.spc == 0 || (g.spc & (g.spc - 1)) || g.reserved == 0 || g.nfats < 1 || g.nfats > 2 || g.root_entries == 0 ||
      g.root_entries % 16 || g.fat_sectors == 0 || g.total > img_size / SEC) {
    return DRIVE_FAT_EFORMAT;
  }
  geo_finish(g);
  if (g.clusters < MIN_CLUSTERS || g.clusters > MAX_CLUSTERS || (uint64_t)g.fat_sectors * SEC / 2 < g.clusters + 2) {
    return DRIVE_FAT_EFORMAT;
  }
  return DRIVE_FAT_OK;
}

static uint32_t fat_next(const uint8_t *img, const geo_t &g, uint32_t c) { return rd16(img + g.reserved * SEC + c * 2); }

// Follows a chain from `first`, marking each cluster in `used`; stores up to `max` clusters in out.
// Returns the length, or < 0 if it leaves the volume, loops or meets a used cluster.
static int64_t chain(const uint8_t *img, const geo_t &g, uint32_t first, uint8_t *used, uint32_t *out, uint32_t max) {
  uint32_t c = first;
  uint32_t len = 0;
  while (true) {
    if (c < 2 || c >= g.clusters + 2) return DRIVE_FAT_ECHAIN;
    if (used[c / 8] & (1 << (c % 8))) return DRIVE_FAT_ECHAIN;
    used[c / 8] |= (uint8_t)(1 << (c % 8));
    if (out && len < max) out[len] = c;
    len++;
    uint32_t nx = fat_next(img, g, c);
    if (nx >= 0xfff8) return len;
    c = nx;
  }
}

typedef struct {
  const uint8_t *img;
  geo_t g;
  uint8_t *used;
  bool report; // second pass: call cb
  drive_fat_walk_fn cb;
  void *ctx;
  bool stopped;
} walk_t;

static int walk_dir(walk_t &w, uint32_t first, const char *parent, bool parent_too_long, int depth);

// One directory entry (short entry with its long name decoded); returns < 0 on damage
static int visit(walk_t &w, const uint8_t *e, const char *name, bool name_cut, const char *parent, bool parent_cut,
                 int depth) {
  drive_fat_entry_t out;
  memset(&out, 0, sizeof(out));
  size_t pl = strlen(parent);
  size_t nl = strlen(name);
  out.too_long = parent_cut || name_cut || pl + (pl ? 1 : 0) + nl > DRIVE_FAT_NAME_MAX;
  size_t pos = 0;
  for (size_t i = 0; i < pl && pos < DRIVE_FAT_NAME_MAX; i++) out.path[pos++] = parent[i];
  if (pl && pos < DRIVE_FAT_NAME_MAX) out.path[pos++] = '/';
  for (size_t i = 0; i < nl && pos < DRIVE_FAT_NAME_MAX; i++) out.path[pos++] = name[i];
  out.path[pos] = '\0';
  out.is_dir = (e[11] & ATTR_DIR) != 0;
  out.size = out.is_dir ? 0 : rd32(e + 28);
  out.first_cluster = rd16(e + 26);
  if (!w.report) {
    if (out.is_dir) {
      if (out.first_cluster == 0) return DRIVE_FAT_EDIR;
    } else {
      uint64_t need = ((uint64_t)out.size + csize(w.g) - 1) / csize(w.g);
      if (need == 0) {
        if (out.first_cluster != 0) return DRIVE_FAT_ECHAIN;
      } else {
        int64_t len = chain(w.img, w.g, out.first_cluster, w.used, nullptr, 0);
        if (len < 0) return (int)len;
        if ((uint64_t)len != need) return DRIVE_FAT_ECHAIN;
      }
    }
  } else if (!w.cb(w.ctx, &out)) {
    w.stopped = true;
    return DRIVE_FAT_OK;
  }
  if (out.is_dir) return walk_dir(w, out.first_cluster, out.path, out.too_long, depth + 1);
  return DRIVE_FAT_OK;
}

static int walk_entries(walk_t &w, const uint8_t *const *blocks, uint32_t nblocks, uint32_t per_block,
                        const char *parent, bool parent_cut, int depth) {
  uint16_t lfn[20 * LFN_CHARS];
  int lfn_next = 0; // the sequence number expected next, 0: none in progress
  int lfn_count = 0;
  uint8_t lfn_sum = 0;
  for (uint32_t bi = 0; bi < nblocks; bi++) {
    for (uint32_t k = 0; k < per_block; k++) {
      const uint8_t *e = blocks[bi] + k * 32;
      if (e[0] == 0x00) return DRIVE_FAT_OK;
      if (e[0] == 0xe5) {
        lfn_next = 0;
        continue;
      }
      if (e[11] == ATTR_LFN) {
        int ord = e[0] & 0x1f;
        if (e[0] & 0x40) {
          lfn_count = ord;
          lfn_next = ord;
          lfn_sum = e[13];
          if (ord < 1 || ord > 20) lfn_next = 0;
        }
        if (lfn_next == 0 || ord != lfn_next || e[13] != lfn_sum) {
          lfn_next = 0;
          continue;
        }
        for (int j = 0; j < LFN_CHARS; j++) lfn[(ord - 1) * LFN_CHARS + j] = rd16(e + lfn_offsets[j]);
        lfn_next = ord - 1 == 0 ? -1 : ord - 1; // -1: complete, waiting for its short entry
        continue;
      }
      bool have_lfn = lfn_next == -1 && lfn_checksum(e) == lfn_sum;
      lfn_next = 0;
      if (e[11] & ATTR_VOLUME) continue;
      if (e[0] == '.') continue; // "." and ".."
      char name[DRIVE_FAT_NAME_MAX * 2 + 8];
      size_t pos = 0;
      bool cut = false;
      if (have_lfn) {
        for (int u = 0; u < lfn_count * LFN_CHARS; u++) {
          uint32_t cp = lfn[u];
          if (cp == 0) break;
          if (cp >= 0xd800 && cp <= 0xdbff && u + 1 < lfn_count * LFN_CHARS && lfn[u + 1] >= 0xdc00 &&
              lfn[u + 1] <= 0xdfff) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (lfn[u + 1] - 0xdc00);
            u++;
          } else if (cp >= 0xd800 && cp <= 0xdfff) {
            cp = 0x7f; // a lone surrogate: a name drive_name_ok refuses
          }
          if (!put_utf8(name, pos, sizeof(name), cp)) {
            cut = true;
            break;
          }
        }
      } else {
        for (int i = 0; i < 11; i++) {
          if (i == 8 && e[8] != ' ') name[pos++] = '.';
          uint8_t c = e[i];
          if (c == ' ') continue;
          if (i == 0 && c == 0x05) c = 0xe5;
          bool lower = (i < 8 && (e[12] & 0x08)) || (i >= 8 && (e[12] & 0x10));
          if (lower && c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
          name[pos++] = c >= 0x80 ? 0x7f : (char)c; // OEM code page: refused as a name
        }
      }
      name[pos] = '\0';
      int rc = visit(w, e, name, cut, parent, parent_cut, depth);
      if (rc < 0 || w.stopped) return rc;
    }
  }
  return DRIVE_FAT_OK;
}

static int walk_dir(walk_t &w, uint32_t first, const char *parent, bool parent_cut, int depth) {
  if (depth > MAX_DEPTH) return DRIVE_FAT_EDIR;
  const uint8_t *blocks[DIR_MAX_CLUSTERS];
  uint32_t nblocks;
  uint32_t per_block;
  if (first == 0) {
    blocks[0] = w.img + w.g.root_start * SEC;
    nblocks = 1;
    per_block = w.g.root_entries;
  } else {
    uint32_t clusters[DIR_MAX_CLUSTERS];
    int64_t len;
    if (!w.report) {
      len = chain(w.img, w.g, first, w.used, clusters, DIR_MAX_CLUSTERS);
      if (len < 0) return (int)len;
    } else {
      // Checked in the first pass: follow it without marking
      len = 0;
      for (uint32_t c = first; c < 0xfff8 && len < DIR_MAX_CLUSTERS; c = fat_next(w.img, w.g, c)) clusters[len++] = c;
    }
    if (len > DIR_MAX_CLUSTERS) return DRIVE_FAT_EDIR;
    nblocks = (uint32_t)len;
    for (uint32_t i = 0; i < nblocks; i++) blocks[i] = w.img + cluster_offset(w.g, clusters[i]);
    per_block = csize(w.g) / 32;
  }
  return walk_entries(w, blocks, nblocks, per_block, parent, parent_cut, depth);
}

size_t drive_fat_scratch_size(size_t img_size) { return img_size / SEC / 8 + 2; }

int drive_fat_walk(const uint8_t *img, size_t img_size, uint8_t *scratch, size_t scratch_len, drive_fat_walk_fn cb,
                   void *ctx) {
  if (!img || !cb || !scratch) return DRIVE_FAT_EINVAL;
  walk_t w = {};
  w.img = img;
  int rc = parse_geo(img, img_size, w.g);
  if (rc < 0) return rc;
  if (scratch_len < (w.g.clusters + 2) / 8 + 1) return DRIVE_FAT_EINVAL;
  memset(scratch, 0, (w.g.clusters + 2) / 8 + 1);
  w.used = scratch;
  w.cb = cb;
  w.ctx = ctx;
  rc = walk_dir(w, 0, "", false, 0);
  if (rc < 0) return rc;
  w.report = true;
  return walk_dir(w, 0, "", false, 0);
}

int drive_fat_read(const uint8_t *img, size_t img_size, const drive_fat_entry_t *e, uint32_t offset, uint8_t *buf,
                   uint32_t len) {
  geo_t g;
  if (!img || !e || (!buf && len) || e->is_dir) return DRIVE_FAT_EINVAL;
  int rc = parse_geo(img, img_size, g);
  if (rc < 0) return rc;
  if (offset >= e->size) return 0;
  if (len > e->size - offset) len = e->size - offset;
  uint32_t cs = csize(g);
  uint32_t c = e->first_cluster;
  uint32_t skip = offset / cs;
  for (uint32_t i = 0; i < skip; i++) {
    c = fat_next(img, g, c);
    if (c < 2 || c >= g.clusters + 2) return DRIVE_FAT_ECHAIN;
  }
  uint32_t done = 0;
  uint32_t in = offset % cs;
  while (done < len) {
    if (c < 2 || c >= g.clusters + 2) return DRIVE_FAT_ECHAIN;
    uint32_t n = cs - in;
    if (n > len - done) n = len - done;
    memcpy(buf + done, img + cluster_offset(g, c) + in, n);
    done += n;
    in = 0;
    if (done < len) c = fat_next(img, g, c);
  }
  return (int)done;
}
