#include "app_resume.h"

#include <string.h>

app_resume_t app_resume_take(Persistence &p, bool crashed, char *path, size_t path_len) {
  if (!p.readBool(APP_RESUME_FLAG_KEY, false)) return APP_RESUME_NONE;
  p.writeBool(APP_RESUME_FLAG_KEY, false);
  if (crashed) return APP_RESUME_NONE;
  String last = p.readString(APP_RESUME_PATH_KEY, "");
  if (last.length() == 0 || last.length() >= path_len) return APP_RESUME_NONE;
  memcpy(path, last.c_str(), last.length() + 1);
  return p.readBool(APP_RESUME_INPUT_KEY, false) ? APP_RESUME_GO : APP_RESUME_NO_INPUT;
}

void app_resume_input(Persistence &p) { p.writeBool(APP_RESUME_INPUT_KEY, true); }

void app_resume_starting(Persistence &p, const char *path) {
  p.writeBool(APP_RESUME_FLAG_KEY, false);
  p.writeBool(APP_RESUME_INPUT_KEY, false);
  p.writeString(APP_RESUME_PATH_KEY, path);
}

void app_resume_started(Persistence &p) { p.writeBool(APP_RESUME_FLAG_KEY, true); }

void app_resume_ended(Persistence &p) { p.writeBool(APP_RESUME_FLAG_KEY, false); }
