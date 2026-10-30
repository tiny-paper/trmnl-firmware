// Crashes as it starts: a store to address 0 (a LoadStoreError panic), before it draws anything
#include <app_api.h>

APP_INFO("Crash", 0);

int app_main(const app_api_t *api) {
  api->log("crash: crashing now");
  *(volatile int *)0 = 1;
  return 0;
}
