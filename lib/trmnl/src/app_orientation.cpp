#include "app_orientation.h"

int app_orientation(int img_w, int img_h, int panel_w, int panel_h) {
  if (img_w <= 0 || img_h <= 0 || panel_w <= 0 || panel_h <= 0) return APP_ENOENT;
  if (img_w == panel_w && img_h == panel_h) return APP_ORIENT_LANDSCAPE;
  if (panel_w != panel_h && img_w == panel_h && img_h == panel_w) return APP_ORIENT_PORTRAIT;
  return APP_ENOENT;
}
