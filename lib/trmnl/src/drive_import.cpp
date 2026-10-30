// The computer's changes from the drive's FAT image back to the X (drive_sync.h).
//
// Whole or not at all: every file to write is first staged under .drive/stage/ and read back;
// then .drive/plan (stage -> target, written as plan.tmp and renamed: the commit) lists the
// renames; then each staged file is renamed over its target (LittleFS renames atomically) and the
// plan removed. Cut before the commit: drive_recover drops the stage. Cut after it: it renames the
// rest. Either way the files are all old or all new.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drive_sync.h"

#define CHUNK 4096
#define PLAN_MAX (DRIVE_MAX_FILES + 2)
#define SPACE_PER_FILE 4096 // LittleFS metadata and the last partial block

// ---- helpers ----

static bool fs_hash(const drive_fs_t *fs, const char *path, uint8_t *buf, uint64_t *hash, int64_t *size) {
  *size = fs->size(fs->ctx, path);
  *hash = DRIVE_HASH_INIT;
  if (*size < 0) return true; // no file: not an error
  for (int64_t off = 0; off < *size;) {
    int n = fs->read(fs->ctx, path, (uint32_t)off, buf, CHUNK);
    if (n <= 0) return false;
    *hash = drive_hash(*hash, buf, (size_t)n);
    off += n;
  }
  return true;
}

static bool exists_on(void *ctx, const char *path) {
  const drive_fs_t *fs = (const drive_fs_t *)ctx;
  return fs->size(fs->ctx, path) >= 0;
}

// Makes the folders of `path` (documents/<folder>/, settings/fonts/)
static int make_parents(const drive_fs_t *fs, const char *path) {
  char dir[DRIVE_PATH_MAX + 1];
  for (const char *s = strchr(path, '/'); s; s = strchr(s + 1, '/')) {
    size_t len = (size_t)(s - path);
    if (len >= sizeof(dir)) return DRIVE_EINVAL;
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (fs->mkdir(fs->ctx, dir) < 0) return DRIVE_EIO;
  }
  return DRIVE_OK;
}

static bool remove_one(void *ctx, const char *name, bool is_dir, uint32_t size) {
  (void)size;
  const drive_fs_t *fs = (const drive_fs_t *)ctx;
  char path[DRIVE_PATH_MAX + 2];
  if (!is_dir && (size_t)snprintf(path, sizeof(path), DRIVE_STAGE_DIR "/%s", name) < sizeof(path)) {
    fs->remove(fs->ctx, path);
  }
  return true;
}

// Removes what an import cut before its commit left; writes nothing when there is nothing
static void drop_stage(const drive_fs_t *fs) {
  if (fs->list(fs->ctx, DRIVE_STAGE_DIR, remove_one, (void *)fs) >= 0) fs->remove(fs->ctx, DRIVE_STAGE_DIR);
  if (fs->size(fs->ctx, DRIVE_PLAN_TMP) >= 0) fs->remove(fs->ctx, DRIVE_PLAN_TMP);
}

// ---- the journal ----

typedef struct {
  char stage[24];
  char target[DRIVE_PATH_MAX + 1];
} plan_line_t;

// Where a plan may write: a layout file, a conflict copy of one, the conflict list
static bool target_ok(const char *t) {
  if (strcmp(t, DRIVE_CONFLICTS) == 0) return true;
  if (!drive_path_ok(t)) return false;
  return strncmp(t, "documents/", 10) == 0 || strncmp(t, "settings/", 9) == 0;
}

// Reads .drive/plan; returns the line count, 0 if there is none, < 0 if it is damaged
static int read_plan(const drive_fs_t *fs, plan_line_t *lines, int max) {
  int64_t size = fs->size(fs->ctx, DRIVE_PLAN);
  if (size < 0) return 0;
  if (size > (int64_t)max * (int64_t)sizeof(plan_line_t) + 8) return DRIVE_EIO;
  char *text = (char *)malloc((size_t)size + 1);
  if (!text) return DRIVE_ENOMEM;
  int got = size ? fs->read(fs->ctx, DRIVE_PLAN, 0, (uint8_t *)text, (uint32_t)size) : 0;
  if (got != size) {
    free(text);
    return DRIVE_EIO;
  }
  text[size] = '\0';
  int n = 0;
  bool ended = false;
  for (char *line = text; *line && !ended;) {
    char *nl = strchr(line, '\n');
    if (!nl) break;
    *nl = '\0';
    if (strcmp(line, "end") == 0) {
      ended = true;
    } else {
      char *tab = strchr(line, '\t');
      if (!tab || n == max || (size_t)(tab - line) >= sizeof(lines[n].stage) ||
          strncmp(line, DRIVE_STAGE_DIR "/", sizeof(DRIVE_STAGE_DIR)) != 0 || strlen(tab + 1) > DRIVE_PATH_MAX) {
        break;
      }
      memcpy(lines[n].stage, line, (size_t)(tab - line));
      lines[n].stage[tab - line] = '\0';
      snprintf(lines[n].target, sizeof(lines[n].target), "%s", tab + 1);
      if (!target_ok(lines[n].target)) break;
      n++;
    }
    line = nl + 1;
  }
  free(text);
  return ended ? n : DRIVE_EIO;
}

// Renames what is still staged over its target, then ends the transaction
static int apply_plan(const drive_fs_t *fs, const plan_line_t *lines, int n) {
  for (int i = 0; i < n; i++) {
    if (fs->size(fs->ctx, lines[i].stage) < 0) continue; // renamed before the cut
    if (make_parents(fs, lines[i].target) < 0) return DRIVE_EIO;
    if (fs->rename(fs->ctx, lines[i].stage, lines[i].target) < 0) return DRIVE_EIO;
  }
  if (fs->remove(fs->ctx, DRIVE_PLAN) < 0) return DRIVE_EIO;
  drop_stage(fs);
  return DRIVE_OK;
}

int drive_recover(const drive_fs_t *fs) {
  if (!fs) return DRIVE_EINVAL;
  plan_line_t *lines = (plan_line_t *)calloc(PLAN_MAX, sizeof(plan_line_t));
  if (!lines) return DRIVE_ENOMEM;
  int n = read_plan(fs, lines, PLAN_MAX);
  int rc = DRIVE_OK;
  if (n > 0 || (n == 0 && fs->size(fs->ctx, DRIVE_PLAN) >= 0)) {
    rc = apply_plan(fs, lines, n);
  } else if (n < 0) {
    // Never renamed into place by us: a plan we cannot read is a bug or damage. Its staged files
    // may be half the import, so nothing is applied; the stage is kept for a person to look at.
    rc = n;
  } else {
    drop_stage(fs);
  }
  free(lines);
  return rc;
}

// ---- import ----

typedef enum { ACT_NONE, ACT_ADD, ACT_UPDATE, ACT_CONFLICT } act_t;

typedef struct {
  drive_fat_entry_t e;
  uint8_t act;
  char target[DRIVE_PATH_MAX + 1];
} host_file_t;

typedef struct {
  host_file_t *files;
  size_t n;
  bool too_many;
  drive_import_result_t *res;
} gather_t;

static bool starts_with(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

// A part starting with '.': the computer's own (".Trashes", "._x.md")
static bool hidden(const char *path) {
  for (const char *p = path;;) {
    if (*p == '.') return true;
    const char *slash = strchr(p, '/');
    if (!slash) return false;
    p = slash + 1;
  }
}

static bool gather(void *ctx, const drive_fat_entry_t *e) {
  gather_t &g = *(gather_t *)ctx;
  if (e->is_dir) return true;
  if (!e->too_long && drive_layout_ok(e->path)) {
    if (g.n == DRIVE_MAX_FILES) {
      g.too_many = true;
      return false;
    }
    g.files[g.n].e = *e;
    g.files[g.n].act = ACT_NONE;
    g.n++;
  } else if (hidden(e->path)) {
    // not counted
  } else if (starts_with(e->path, "documents/") || starts_with(e->path, "settings/")) {
    g.res->bad_names++;
  } else {
    g.res->left_alone++;
  }
  return true;
}

static const drive_manifest_entry_t *find(const drive_manifest_t *m, const char *path) {
  for (size_t i = 0; i < m->n; i++) {
    if (strcmp(m->e[i].path, path) == 0) return &m->e[i];
  }
  return nullptr;
}

static bool on_host(const host_file_t *files, size_t n, const char *path) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(files[i].e.path, path) == 0) return true;
  }
  return false;
}

// A name for a conflict copy is free if it is on neither side and no earlier write of this import
typedef struct {
  const drive_fs_t *fs;
  const gather_t *g;
  size_t before;
} taken_t;

static bool taken(void *ctx, const char *path) {
  const taken_t &t = *(const taken_t *)ctx;
  if (exists_on((void *)t.fs, path) || on_host(t.g->files, t.g->n, path)) return true;
  for (size_t j = 0; j < t.before; j++) {
    if (strcmp(t.g->files[j].target, path) == 0) return true;
  }
  return false;
}

// Decides each host file against the manifest and the X; fills target for writes
static int decide(const drive_fs_t *fs, const uint8_t *img, size_t img_size, const drive_manifest_t *m, gather_t &g,
                  uint8_t *buf, uint64_t *bytes) {
  for (size_t i = 0; i < g.n; i++) {
    host_file_t &f = g.files[i];
    uint64_t host = DRIVE_HASH_INIT;
    for (uint32_t off = 0; off < f.e.size;) {
      int n = drive_fat_read(img, img_size, &f.e, off, buf, CHUNK);
      if (n <= 0) return DRIVE_EIMAGE;
      host = drive_hash(host, buf, (size_t)n);
      off += (uint32_t)n;
    }
    uint64_t x;
    int64_t x_size;
    if (!fs_hash(fs, f.e.path, buf, &x, &x_size)) return DRIVE_EIO;
    const drive_manifest_entry_t *base = find(m, f.e.path);
    bool host_changed = !base || base->hash != host || base->size != f.e.size;
    bool x_same_as_host = x_size == (int64_t)f.e.size && x == host;
    bool x_changed = base ? (x_size < 0 || x != base->hash || x_size != (int64_t)base->size) : x_size >= 0;
    if (!host_changed || x_same_as_host) {
      g.res->unchanged++;
      continue;
    }
    if (x_size < 0) {
      f.act = base ? ACT_UPDATE : ACT_ADD; // deleted on the X meanwhile: the computer's is the only copy
    } else if (!x_changed) {
      f.act = ACT_UPDATE;
    } else {
      f.act = ACT_CONFLICT;
    }
    snprintf(f.target, sizeof(f.target), "%.*s", DRIVE_PATH_MAX, f.e.path); // layout paths fit
    if (f.act == ACT_CONFLICT) {
      taken_t t = {fs, &g, i};
      if (!drive_conflict_name(f.e.path, taken, &t, f.target, sizeof(f.target))) {
        g.res->bad_names++; // no name fits for the copy: the X's file stays, the computer's is not taken
        f.act = ACT_NONE;
        f.target[0] = '\0';
        continue;
      }
    }
    *bytes += f.e.size + SPACE_PER_FILE;
  }
  for (size_t i = 0; i < m->n; i++) {
    if (!on_host(g.files, g.n, m->e[i].path)) g.res->kept_deleted++;
  }
  return DRIVE_OK;
}

static int stage_file(const drive_fs_t *fs, const char *stage, const uint8_t *data_img, size_t img_size,
                      const drive_fat_entry_t *e, uint8_t *buf) {
  uint64_t want = DRIVE_HASH_INIT;
  if (e->size == 0 && fs->write(fs->ctx, stage, buf, 0, false) < 0) return DRIVE_EFULL;
  for (uint32_t off = 0; off < e->size;) {
    int n = drive_fat_read(data_img, img_size, e, off, buf, CHUNK);
    if (n <= 0) return DRIVE_EIMAGE;
    if (fs->write(fs->ctx, stage, buf, (uint32_t)n, off != 0) < 0) return DRIVE_EFULL;
    want = drive_hash(want, buf, (size_t)n);
    off += (uint32_t)n;
  }
  // Read back: a short or torn write must not reach the commit
  uint64_t got;
  int64_t size;
  if (!fs_hash(fs, stage, buf, &got, &size) || size != (int64_t)e->size || got != want) return DRIVE_EIO;
  return DRIVE_OK;
}

// The conflict list with the new pairs appended, staged
static int stage_conflicts(const drive_fs_t *fs, const char *stage, const gather_t &g, uint8_t *buf) {
  if (fs->write(fs->ctx, stage, buf, 0, false) < 0) return DRIVE_EFULL;
  int64_t old = fs->size(fs->ctx, DRIVE_CONFLICTS);
  for (int64_t off = 0; off < old;) {
    int n = fs->read(fs->ctx, DRIVE_CONFLICTS, (uint32_t)off, buf, CHUNK);
    if (n <= 0) return DRIVE_EIO;
    if (fs->write(fs->ctx, stage, buf, (uint32_t)n, true) < 0) return DRIVE_EFULL;
    off += n;
  }
  for (size_t i = 0; i < g.n; i++) {
    if (g.files[i].act != ACT_CONFLICT) continue;
    char line[2 * DRIVE_PATH_MAX + 4];
    int len = snprintf(line, sizeof(line), "%s\t%s\n", g.files[i].e.path, g.files[i].target);
    if (fs->write(fs->ctx, stage, (const uint8_t *)line, (uint32_t)len, true) < 0) return DRIVE_EFULL;
  }
  return DRIVE_OK;
}

static int commit(const drive_fs_t *fs, const gather_t &g, size_t conflicts, plan_line_t *lines, uint8_t *buf,
                  const uint8_t *img, size_t img_size) {
  if (fs->mkdir(fs->ctx, DRIVE_DIR) < 0 || fs->mkdir(fs->ctx, DRIVE_STAGE_DIR) < 0) return DRIVE_EIO;
  int n = 0;
  for (size_t i = 0; i < g.n; i++) {
    if (g.files[i].act == ACT_NONE) continue;
    snprintf(lines[n].stage, sizeof(lines[n].stage), DRIVE_STAGE_DIR "/%d", n);
    snprintf(lines[n].target, sizeof(lines[n].target), "%s", g.files[i].target);
    int rc = stage_file(fs, lines[n].stage, img, img_size, &g.files[i].e, buf);
    if (rc < 0) return rc;
    n++;
  }
  if (conflicts) {
    snprintf(lines[n].stage, sizeof(lines[n].stage), DRIVE_STAGE_DIR "/c");
    snprintf(lines[n].target, sizeof(lines[n].target), DRIVE_CONFLICTS);
    int rc = stage_conflicts(fs, lines[n].stage, g, buf);
    if (rc < 0) return rc;
    n++;
  }
  if (fs->write(fs->ctx, DRIVE_PLAN_TMP, buf, 0, false) < 0) return DRIVE_EFULL;
  for (int i = 0; i < n; i++) {
    char line[24 + DRIVE_PATH_MAX + 4];
    int len = snprintf(line, sizeof(line), "%s\t%s\n", lines[i].stage, lines[i].target);
    if (fs->write(fs->ctx, DRIVE_PLAN_TMP, (const uint8_t *)line, (uint32_t)len, true) < 0) return DRIVE_EFULL;
  }
  if (fs->write(fs->ctx, DRIVE_PLAN_TMP, (const uint8_t *)"end\n", 4, true) < 0) return DRIVE_EFULL;
  if (fs->rename(fs->ctx, DRIVE_PLAN_TMP, DRIVE_PLAN) < 0) return DRIVE_EIO; // the commit
  return apply_plan(fs, lines, n);
}

int drive_import(const drive_fs_t *fs, const uint8_t *img, size_t img_size, const drive_manifest_t *m,
                 drive_import_result_t *res) {
  if (!fs || !img || !m || !res) return DRIVE_EINVAL;
  memset(res, 0, sizeof(*res));
  int rc = drive_recover(fs);
  if (rc < 0) return rc;
  size_t scratch_len = drive_fat_scratch_size(img_size);
  uint8_t *scratch = (uint8_t *)malloc(scratch_len);
  uint8_t *buf = (uint8_t *)malloc(CHUNK);
  host_file_t *files = (host_file_t *)calloc(DRIVE_MAX_FILES, sizeof(host_file_t));
  plan_line_t *lines = (plan_line_t *)calloc(PLAN_MAX, sizeof(plan_line_t));
  gather_t g = {files, 0, false, res};
  if (!scratch || !buf || !files || !lines) rc = DRIVE_ENOMEM;
  if (rc == DRIVE_OK && drive_fat_walk(img, img_size, scratch, scratch_len, gather, &g) < 0) rc = DRIVE_EIMAGE;
  if (rc == DRIVE_OK && g.too_many) rc = DRIVE_ETOOMANY;
  uint64_t bytes = 0;
  if (rc == DRIVE_OK) rc = decide(fs, img, img_size, m, g, buf, &bytes);
  size_t writes = 0, conflicts = 0;
  for (size_t i = 0; rc == DRIVE_OK && i < g.n; i++) {
    writes += g.files[i].act != ACT_NONE;
    conflicts += g.files[i].act == ACT_CONFLICT;
  }
  if (rc == DRIVE_OK && conflicts) bytes += SPACE_PER_FILE + (uint64_t)(conflicts + 64) * (2 * DRIVE_PATH_MAX + 2);
  if (rc == DRIVE_OK && writes && (int64_t)(bytes + 2 * SPACE_PER_FILE) > fs->free_bytes(fs->ctx)) rc = DRIVE_EFULL;
  if (rc == DRIVE_OK && writes) {
    rc = commit(fs, g, conflicts, lines, buf, img, img_size);
    // Cut before the commit: nothing changed, the stage goes. After it: drive_recover finishes.
    if (rc < 0 && fs->size(fs->ctx, DRIVE_PLAN) < 0) drop_stage(fs);
  }
  if (rc == DRIVE_OK) {
    for (size_t i = 0; i < g.n; i++) {
      res->added += g.files[i].act == ACT_ADD;
      res->updated += g.files[i].act == ACT_UPDATE;
      res->conflicts += g.files[i].act == ACT_CONFLICT;
    }
  } else {
    drive_import_result_t keep = *res;
    memset(res, 0, sizeof(*res));
    res->left_alone = keep.left_alone;
    res->bad_names = keep.bad_names;
  }
  free(scratch);
  free(buf);
  free(files);
  free(lines);
  return rc;
}
