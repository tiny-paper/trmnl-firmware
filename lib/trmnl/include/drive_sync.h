#pragma once

// Moves the writer's files to the USB drive's FAT image and back (docs/apps.md, "USB drive").
// Owner rules: nothing is overwritten silently (a file changed on both sides is kept twice and
// listed for the user to choose on the X), files outside the layout are left alone, and an import
// lands whole or not at all, also across a power loss (a journal under .drive/, see drive_recover).

#include <stddef.h>
#include <stdint.h>

#include "drive_fat.h"
#include "drive_layout.h"

#define DRIVE_MAX_FILES 512
#define DRIVE_DIR ".drive"
#define DRIVE_STAGE_DIR ".drive/stage"
#define DRIVE_PLAN ".drive/plan"
#define DRIVE_PLAN_TMP ".drive/plan.tmp"
// One line per kept pair: "<the X's file>\t<the computer's copy>\n"; the app clears a line once the
// user chose
#define DRIVE_CONFLICTS ".drive/conflicts.txt"

typedef enum {
  DRIVE_OK = 0,
  DRIVE_EINVAL = -1,
  DRIVE_ENOMEM = -3,
  DRIVE_EIO = -4, // a read or write failed (nothing changed, or the journal finishes it next time)
  DRIVE_EIMAGE = -10, // the drive's image is damaged or reformatted: nothing imported
  DRIVE_EFULL = -11, // not enough room on the X (import) or on the drive (export): nothing changed
  DRIVE_ETOOMANY = -12, // more than DRIVE_MAX_FILES files
} drive_result_t;

// The app folder's file system (paths relative to it). Device: LittleFS; tests: a fake.
typedef bool (*drive_list_fn)(void *cbctx, const char *name, bool is_dir, uint32_t size);
typedef struct {
  void *ctx;
  int64_t (*size)(void *ctx, const char *path); // a file's size; < 0 if no file there
  int (*read)(void *ctx, const char *path, uint32_t offset, uint8_t *buf, uint32_t len); // count, < 0 on error
  // Creates or truncates (append false), then writes; returns 0, or < 0 (full disk: short write)
  int (*write)(void *ctx, const char *path, const uint8_t *buf, uint32_t len, bool append);
  int (*rename)(void *ctx, const char *from, const char *to); // atomically replaces `to`
  int (*remove)(void *ctx, const char *path); // a file or an empty folder
  int (*mkdir)(void *ctx, const char *path); // 0 also when it exists
  int (*list)(void *ctx, const char *dir, drive_list_fn cb, void *cbctx); // < 0 if no such folder
  int64_t (*free_bytes)(void *ctx);
} drive_fs_t;

// What the drive showed, to tell later who changed what
typedef struct {
  char path[DRIVE_PATH_MAX + 1];
  uint32_t size;
  uint64_t hash;
} drive_manifest_entry_t;

typedef struct {
  drive_manifest_entry_t *e; // DRIVE_MAX_FILES entries, the caller's
  size_t n;
} drive_manifest_t;

typedef struct {
  uint32_t shown; // files on the drive
  uint32_t not_shown; // layout files the drive cannot hold (a name differing only in case)
} drive_export_result_t;

// Builds the drive's image from the layout files and fills the manifest
int drive_export(const drive_fs_t *fs, uint8_t *img, size_t img_size, drive_manifest_t *m, uint32_t fat_time,
                 drive_export_result_t *res);

typedef struct {
  uint32_t added; // new from the computer
  uint32_t updated; // changed on the computer only
  uint32_t conflicts; // changed on both sides: the computer's copy kept beside, listed in DRIVE_CONFLICTS
  uint32_t unchanged;
  uint32_t kept_deleted; // deleted on the computer: kept on the X (deletes are not taken)
  uint32_t bad_names; // under documents/ or settings/ but not a layout name (too long, wrong type)
  uint32_t left_alone; // anything else on the drive
} drive_import_result_t;

// Takes the computer's changes from the image, whole or not at all. Runs drive_recover first.
int drive_import(const drive_fs_t *fs, const uint8_t *img, size_t img_size, const drive_manifest_t *m,
                 drive_import_result_t *res);

// Finishes an import that was cut after its commit, or drops one cut before it. Call before the
// app reads its files. 0 when nothing was pending or it is done.
int drive_recover(const drive_fs_t *fs);
