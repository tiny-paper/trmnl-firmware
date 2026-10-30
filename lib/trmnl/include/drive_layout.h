#pragma once

// The writer's files as the USB drive shows them (docs/apps.md, "USB drive"): which paths take part
// in a transfer, the names a FAT drive and LittleFS both accept, conflict copy names, content hashes.
// Paths are relative to the app's folder, '/' separated.

#include <stddef.h>
#include <stdint.h>

// Longest relative path: the app interface's limit (APP_PATH_MAX)
#define DRIVE_PATH_MAX 64

// One name (file or folder): not empty, valid UTF-8, no control characters, none of \ / : * ? " < > |,
// not starting with '.' (hidden: macOS "._x.md", ".Trashes") and not ending in '.' or ' '
bool drive_name_ok(const char *name, size_t len);

// 1 to 3 parts, each drive_name_ok, at most DRIVE_PATH_MAX bytes in all
bool drive_path_ok(const char *path);

// Part of the writer's layout: documents/<folder>/<title>.md|.txt, settings/settings.json,
// settings/fonts/<file>; every name drive_name_ok, the whole at most DRIVE_PATH_MAX bytes.
// Anything else is left alone by every transfer.
bool drive_layout_ok(const char *path);

// The name a copy from the computer gets when both sides changed: "a/b.md" -> "a/b (computer).md",
// then "(computer 2)" ... while exists(ctx, candidate). False if no name fits DRIVE_PATH_MAX.
typedef bool (*drive_exists_fn)(void *ctx, const char *path);
bool drive_conflict_name(const char *path, drive_exists_fn exists, void *ctx, char *out, size_t out_len);

// FNV-1a 64 over the content, fed in pieces
#define DRIVE_HASH_INIT 0xcbf29ce484222325ull
uint64_t drive_hash(uint64_t h, const uint8_t *data, size_t len);

// ASCII case-insensitive equality: the FAT drive cannot hold two names that differ only so
bool drive_same_name_on_fat(const char *a, const char *b);
