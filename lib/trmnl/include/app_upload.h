#pragma once

#include <stddef.h>
#include <stdint.h>

// Copying an app to /apps over the USB serial port, without replacing the file system (docs/apps.md,
// "Copy an app to the X"; host side apps/upload.py). The firmware's side of the protocol, I/O free:
//
//   host:   TAPP PUT <name>.elf <size> <crc32 hex>\n
//   device: TAPP READY                 (or TAPP ERR <why>)
//   host:   up to APP_UPLOAD_BLOCK bytes, then waits for
//   device: TAPP ACK <bytes so far>    ... until all <size> bytes came
//   device: TAPP OK <name>.elf         (or TAPP ERR <why>; the old file, if any, is kept)
//
// The data goes to a temporary file, renamed over /apps/<name>.elf only when size and CRC-32 (zlib's) match.

#define APP_UPLOAD_BLOCK      64 // bytes per ACK: one USB packet, well inside the serial RX buffer (256), which drops what does not fit
#define APP_UPLOAD_MAX        (2u * 1024u * 1024u)
#define APP_UPLOAD_MARGIN     (16u * 1024u) // left free on the file system
#define APP_UPLOAD_IDLE_MS    5000 // no byte for this long: the upload is given up
#define APP_UPLOAD_LINE_MAX   96
#define APP_UPLOAD_TMP        "/apps/.upload.tmp"

typedef enum {
  APP_UPLOAD_OK,
  APP_UPLOAD_BAD_HEADER, // not "TAPP PUT <name> <size> <crc>"
  APP_UPLOAD_BAD_NAME, // not <name>.elf with an app name (app_path_name)
  APP_UPLOAD_TOO_BIG, // over APP_UPLOAD_MAX
  APP_UPLOAD_NO_SPACE, // more than the file system has free, less the margin
  APP_UPLOAD_EMPTY, // size 0
  APP_UPLOAD_SHORT, // fewer bytes than announced came before the idle timeout
  APP_UPLOAD_BAD_CRC,
  APP_UPLOAD_WRITE_FAILED,
} app_upload_result_t;

typedef struct {
  char path[48]; // /apps/<name>.elf
  uint32_t size;
  uint32_t crc;
} app_upload_header_t;

// Parses a header line (without its newline) and checks it against the free bytes of the file system
app_upload_result_t app_upload_parse(const char *line, uint64_t fs_free, app_upload_header_t *h);

// The CRC is trmnl_crc32 (trmnl_crc32.h)

// The result once the data ended: `got` bytes with CRC `crc`
app_upload_result_t app_upload_check(const app_upload_header_t *h, uint32_t got, uint32_t crc);

// The short reason after "TAPP ERR "
const char *app_upload_result_str(app_upload_result_t r);
