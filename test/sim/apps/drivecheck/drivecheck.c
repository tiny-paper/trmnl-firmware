// The USB drive's firmware side without a computer (test_drive.py): an interrupted import is
// finished before the app runs, and a drive no computer takes ends by itself with serial back
#include <app_api.h>

APP_INFO("Drivecheck", 0);

static int exists(const app_api_t *api, const char *path) {
  app_file_t *f = api->file_open(path, APP_FILE_READ);
  if (!f) return 0;
  api->file_close(f);
  return 1;
}

int app_main(const app_api_t *api) {
  char buf[64] = {0};
  app_file_t *f = api->file_open("documents/n/a.md", APP_FILE_READ);
  int n = f ? api->file_read(f, buf, sizeof(buf) - 1) : -1;
  if (f) api->file_close(f);
  if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = 0;
  api->log("drivecheck: a.md=%s plan=%d stage=%d plantmp=%d", n >= 0 ? buf : "(none)", exists(api, ".drive/plan"),
           exists(api, ".drive/stage/0"), exists(api, ".drive/plan.tmp"));
  if (!exists(api, "drive-on")) return 0;

  // The drive with no computer: shown, writes refused, ends by its timeout, writes work again
  int rc = api->usb_drive_start(1500);
  api->log("drivecheck: start %d state %d", rc, api->usb_drive_state());
  app_file_t *w = api->file_open("documents/n/b.md", APP_FILE_WRITE);
  api->log("drivecheck: write while shown %s", w ? "allowed" : "refused");
  if (w) api->file_close(w);
  for (int i = 0; i < 100 && api->usb_drive_state() != APP_DRIVE_OFF; i++) api->sleep_ms(100);
  app_drive_result_t res;
  rc = api->usb_drive_stop(&res);
  api->log("drivecheck: stop %d ended_by %d shown %d taken %d", rc, res.ended_by, res.shown, res.taken);
  w = api->file_open("documents/n/b.md", APP_FILE_WRITE);
  api->log("drivecheck: write after %s", w ? "allowed" : "refused");
  if (w) api->file_close(w);
  return 0;
}
