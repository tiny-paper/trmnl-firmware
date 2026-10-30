// Checks a few calls, then returns an error code: the firmware shows it and goes back to TRMNL
#include <app_api.h>

APP_INFO("Fail", 0);

int app_main(const app_api_t *api) {
  // A path out of its folder is refused
  if (api->file_open("../../system/x", APP_FILE_WRITE)) return 10;
  app_file_t *f = api->file_open("note.txt", APP_FILE_WRITE);
  if (!f) return 11;
  if (api->file_write(f, "hi", 2) != 2) return 12;
  // left open on purpose: the firmware closes it
  api->log("fail: returning 7");
  return 7;
}
