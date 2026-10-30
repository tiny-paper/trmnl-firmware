#pragma once

#include <stdbool.h>
#include <stddef.h>

// Which files the Wi-Fi page touches. The writer keeps, under its app folder:
//   documents/<folder>/<title>.md|.txt     documents
//   settings/fonts/<name>.ttf|.otf         fonts (added from the page, never removed by it)
//   settings/settings.json                 not offered: left alone
// Anything else is "other": listed nowhere, never read, written, renamed or deleted by the page.

#define WS_NAME_MAX 40 // bytes in one name (folder, title with extension, font)
#define WS_PATH_MAX 64 // APP_PATH_MAX

typedef enum { WS_OTHER, WS_DOC, WS_FONT } ws_kind_t;

// Classifies a relative path. Refused (WS_OTHER): any name drive_name_ok refuses (empty, "." / "..", a leading dot,
// a trailing dot or space, invalid UTF-8, control characters, '\\', ':', '*', '?', '"', '<', '>', '|'), names over
// WS_NAME_MAX, paths over WS_PATH_MAX.
ws_kind_t ws_classify(const char *path);

// One name (a folder, or a title with its extension) on its own, same rules as above
bool ws_name_ok(const char *name);

// "documents/<folder>/<title>.<ext>" -> the same with " (web N)" before the extension, N >= 1.
// False if it does not fit WS_NAME_MAX / WS_PATH_MAX or `path` is not a document or font.
bool ws_alt_name(const char *path, int n, char *out, size_t out_len);
