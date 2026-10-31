#include "app_orientation.h"

int app_orientation(int img_w, int img_h, int panel_w, int panel_h) {
  if (img_w <= 0 || img_h <= 0 || panel_w <= 0 || panel_h <= 0) return APP_ENOENT;
  if (img_w == panel_w && img_h == panel_h) return APP_ORIENT_LANDSCAPE;
  if (panel_w != panel_h && img_w == panel_h && img_h == panel_w) return APP_ORIENT_PORTRAIT;
  return APP_ENOENT;
}

static bool is_position(int o) { return o >= APP_ORIENT_BAR_BOTTOM && o <= APP_ORIENT_BAR_RIGHT; }

int app_orientation_screen(int sensor, int from_image) {
  if (!is_position(sensor)) return from_image;
  return sensor == APP_ORIENT_BAR_LEFT || sensor == APP_ORIENT_BAR_RIGHT ? APP_ORIENT_PORTRAIT : APP_ORIENT_LANDSCAPE;
}

int app_orientation_device(int sensor, int from_image) {
  if (is_position(sensor)) return sensor;
  return from_image == APP_ORIENT_PORTRAIT ? APP_ORIENT_BAR_LEFT : APP_ORIENT_BAR_BOTTOM;
}
