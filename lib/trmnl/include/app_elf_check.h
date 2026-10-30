#pragma once

#include <stddef.h>
#include <stdint.h>

#include <app_api.h>

// Checks an app's ELF file (docs/apps.md) before the loader touches it. The loader (Espressif's
// elf_loader) trusts the file: offsets, sizes and indices are not bounds-checked, so a truncated or
// damaged file would read past the buffer. This walks the headers, sections and relocations the
// loader uses, and reads the app's app_info_t.

typedef enum {
  APP_ELF_OK,
  APP_ELF_TRUNCATED, // a header, section or table lies past the end of the file
  APP_ELF_NOT_ELF,
  APP_ELF_WRONG_ARCH, // not a 32-bit little-endian Xtensa shared object or executable
  APP_ELF_BAD_LAYOUT, // inconsistent headers, a bad index or relocation, no .text, entry outside it
  APP_ELF_NO_INFO, // no app_info section
  APP_ELF_BAD_INFO, // bad magic, name not terminated, API version 0
  APP_ELF_API_TOO_NEW, // built for a newer APP_API_VERSION than the firmware's
  APP_ELF_STACK_TOO_BIG, // more than APP_MAX_STACK
  APP_ELF_TOO_BIG, // its sections do not fit in the free memory
} app_elf_result_t;

// Checks `len` bytes of an ELF file. `free_mem`: bytes the loader may still allocate for the app's
// sections (PSRAM); the stack is internal RAM, checked when the task is created. Fills *info
// (stack_size resolved to the default if 0) on APP_ELF_OK, and for the results after APP_ELF_NO_INFO.
app_elf_result_t app_elf_check(const uint8_t *buf, size_t len, size_t free_mem, app_info_t *info);

// A short text for the error screen, e.g. "file is truncated"
const char *app_elf_result_str(app_elf_result_t result);
