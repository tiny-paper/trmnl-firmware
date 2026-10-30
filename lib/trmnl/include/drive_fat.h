#pragma once

// The FAT16 image the USB drive shows (docs/apps.md, "USB drive"): built in RAM from the writer's
// files, served to the computer sector by sector, read back after it let go. The computer only
// ever writes this image, never the flash file system. The reader trusts nothing in the image:
// every field, chain and name is checked, and a damaged image is refused as a whole.

#include <stddef.h>
#include <stdint.h>

#define DRIVE_FAT_SECTOR 512
#define DRIVE_FAT_MIN_BYTES (3u * 1024 * 1024) // FAT16 needs at least 4085 clusters
#define DRIVE_FAT_NAME_MAX 128 // a reader path longer than this is reported as too long

typedef enum {
  DRIVE_FAT_OK = 0,
  DRIVE_FAT_EINVAL = -1, // bad argument, size or name
  DRIVE_FAT_EFULL = -2, // the files do not fit the image
  DRIVE_FAT_EFORMAT = -3, // not our FAT16 (reformatted, damaged boot sector)
  DRIVE_FAT_ECHAIN = -4, // a cluster chain out of range, looping, shared, or not matching a size
  DRIVE_FAT_EDIR = -5, // a directory too deep, too big or damaged
} drive_fat_result_t;

// ---- building ----

typedef struct {
  const char *path; // relative, '/' separated, names drive_name_ok
  uint32_t size;
} drive_fat_file_t;

// Fills dst with the first `len` bytes of file `index` (len is its whole size); false on a read error
typedef bool (*drive_fat_fill_fn)(void *ctx, size_t index, uint8_t *dst, uint32_t len);

// Formats img (img_size bytes, a multiple of DRIVE_FAT_SECTOR, at least DRIVE_FAT_MIN_BYTES) and
// writes the files with their folders. `fat_time` is the FAT date << 16 | time for every entry.
int drive_fat_build(uint8_t *img, size_t img_size, const char *label, const drive_fat_file_t *files, size_t n,
                    drive_fat_fill_fn fill, void *ctx, uint32_t fat_time);

// FAT date and time from a broken-down local time (year 1980..2107)
uint32_t drive_fat_time(int year, int month, int day, int hour, int minute, int second);

// ---- reading ----

typedef struct {
  char path[DRIVE_FAT_NAME_MAX + 1]; // relative, UTF-8
  bool too_long; // the path did not fit: `path` is cut, never use it as a name
  bool is_dir;
  uint32_t size;
  uint32_t first_cluster;
} drive_fat_entry_t;

// Called for every file and folder (folders before their content); return false to stop
typedef bool (*drive_fat_walk_fn)(void *ctx, const drive_fat_entry_t *e);

// Checks the whole image first (boot sector, both FATs' chains, every directory, every file's
// chain against its size, no cluster used twice), then walks it. Nothing is reported from an
// image that fails any check. `scratch` holds img_size / DRIVE_FAT_SECTOR / 8 + 1 bytes.
int drive_fat_walk(const uint8_t *img, size_t img_size, uint8_t *scratch, size_t scratch_len, drive_fat_walk_fn cb,
                   void *ctx);
size_t drive_fat_scratch_size(size_t img_size);

// Copies up to len bytes of a walked file from offset; returns the count, or < 0
int drive_fat_read(const uint8_t *img, size_t img_size, const drive_fat_entry_t *e, uint32_t offset, uint8_t *buf,
                   uint32_t len);
