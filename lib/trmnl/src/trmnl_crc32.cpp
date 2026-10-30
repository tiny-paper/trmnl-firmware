#include "trmnl_crc32.h"

uint32_t trmnl_crc32(uint32_t crc, const void *data, size_t len) {
  const uint8_t *b = (const uint8_t *)data;
  crc = ~crc;
  while (len--) {
    crc ^= *b++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
