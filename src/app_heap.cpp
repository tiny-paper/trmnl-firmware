// An app's allocator in PSRAM first (app_heap.h).
#ifdef BOARD_TRMNL_X

#include "app_heap.h"

#include <esp_heap_caps.h>
#include <private/elf_symbol.h>
#include <string.h>

static const uint32_t PSRAM = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

static void *app_malloc(size_t n) { return heap_caps_malloc_prefer(n, 2, PSRAM, MALLOC_CAP_DEFAULT); }

static void *app_calloc(size_t n, size_t size) {
  return heap_caps_calloc_prefer(n, size, 2, PSRAM, MALLOC_CAP_DEFAULT);
}

static void *app_realloc(void *p, size_t n) { return heap_caps_realloc_prefer(p, n, 2, PSRAM, MALLOC_CAP_DEFAULT); }

static uintptr_t app_resolve(const char *name) {
  if (!strcmp(name, "malloc")) return (uintptr_t)&app_malloc;
  if (!strcmp(name, "calloc")) return (uintptr_t)&app_calloc;
  if (!strcmp(name, "realloc")) return (uintptr_t)&app_realloc;
  return elf_find_sym_default(name); // free() frees either heap
}

void app_heap_route(void) { elf_set_symbol_resolver(app_resolve); }

#endif // BOARD_TRMNL_X
