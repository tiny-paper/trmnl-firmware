#pragma once
// An app's malloc, calloc and realloc take PSRAM first, internal RAM only when PSRAM is full (docs/apps.md
// "Memory for HTTPS"): internal RAM stays for Wi-Fi, the BLE controller, DMA and task stacks. The firmware's
// own malloc keeps the IDF rule (blocks up to CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL internal first).

// Routes the app's allocator symbols for every ELF loaded after it; call once before loading apps
void app_heap_route(void);
