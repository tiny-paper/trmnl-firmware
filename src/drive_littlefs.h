#pragma once

// drive_fs_t (drive_sync.h) on LittleFS under one folder (the app's)
#include <drive_sync.h>

void drive_littlefs(drive_fs_t *fs, const char *base);
