// Asks for more memory than the PSRAM has: 9 MB of .bss
#include <app_api.h>

APP_INFO("Huge", 0);

static unsigned char big[9 * 1024 * 1024];

int app_main(const app_api_t *api) {
  big[api->millis() % sizeof(big)] = 1;
  return big[0];
}
