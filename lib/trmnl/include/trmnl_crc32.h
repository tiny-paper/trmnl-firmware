#pragma once

#include <stddef.h>
#include <stdint.h>

// zlib's CRC-32 (the one in zip files and in apps/upload.py): crc = trmnl_crc32(0, data, len), continued with the
// previous value. The only CRC-32 of the app work (USB serial upload, Wi-Fi page uploads and zip)
uint32_t trmnl_crc32(uint32_t crc, const void *data, size_t len);
