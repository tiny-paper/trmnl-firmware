#include <touchbar_layout.h>

static bool valid(int orient) { return orient >= APP_ORIENT_BAR_BOTTOM && orient <= APP_ORIENT_BAR_RIGHT; }

int touchbar_zone_of_channel(int orient, int channel) {
  if (channel < 0 || channel > 2) return channel;
  // the bar at the bottom or on the left: channel 0 is the user's left (top); at the top or on the right it is turned
  return orient == APP_ORIENT_BAR_TOP || orient == APP_ORIENT_BAR_RIGHT ? 2 - channel : channel;
}

// A framebuffer pixel to the app's frame in `orient` (the inverse of app_orient_to_fb)
static void fb_to_app(int orient, int W, int H, int fx, int fy, int *x, int *y) {
  switch (orient) {
  case APP_ORIENT_BAR_LEFT: *x = H - 1 - fy; *y = fx; break;
  case APP_ORIENT_BAR_TOP: *x = W - 1 - fx; *y = H - 1 - fy; break;
  case APP_ORIENT_BAR_RIGHT: *x = fy; *y = W - 1 - fx; break;
  default: *x = fx; *y = fy; break;
  }
}

int touchbar_layout_fill(int orient, int panel_w, int panel_h, app_touchbar_t *out) {
  if (!out || !valid(orient) || panel_w <= 0 || panel_h <= APP_TOUCHBAR_DEPTH) return APP_EINVAL;
  const bool portrait = orient == APP_ORIENT_BAR_LEFT || orient == APP_ORIENT_BAR_RIGHT;
  out->width = (int16_t)(portrait ? panel_h : panel_w);
  out->height = (int16_t)(portrait ? panel_w : panel_h);
  out->orientation = (uint8_t)orient;
  out->edge = (uint8_t)orient; // APP_EDGE_* has the same order as the positions
  out->reserved = 0;
  for (int ch = 0; ch < 3; ch++) {
    // the channel's third of the bottom strip, inclusive corners in the panel's frame
    const int fx0 = panel_w * ch / 3, fx1 = panel_w * (ch + 1) / 3 - 1;
    const int fy0 = panel_h - APP_TOUCHBAR_DEPTH, fy1 = panel_h - 1;
    int ax0, ay0, ax1, ay1;
    fb_to_app(orient, panel_w, panel_h, fx0, fy0, &ax0, &ay0);
    fb_to_app(orient, panel_w, panel_h, fx1, fy1, &ax1, &ay1);
    const int x = ax0 < ax1 ? ax0 : ax1, y = ay0 < ay1 ? ay0 : ay1;
    const int w = (ax0 < ax1 ? ax1 - ax0 : ax0 - ax1) + 1, h = (ay0 < ay1 ? ay1 - ay0 : ay0 - ay1) + 1;
    out->zone[touchbar_zone_of_channel(orient, ch)] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
  }
  return 0;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

app_rect_t touchbar_hint_box(const app_touchbar_t *bar, int zone, int w, int h, int gap) {
  const app_rect_t &z = bar->zone[clamp(zone, 0, 2)];
  int x = z.x + z.w / 2 - w / 2, y = z.y + z.h / 2 - h / 2;
  switch (bar->edge) {
  case APP_EDGE_BOTTOM: y = bar->height - gap - h; break;
  case APP_EDGE_TOP: y = gap; break;
  case APP_EDGE_LEFT: x = gap; break;
  case APP_EDGE_RIGHT: x = bar->width - gap - w; break;
  }
  x = clamp(x, 0, bar->width - w > 0 ? bar->width - w : 0);
  y = clamp(y, 0, bar->height - h > 0 ? bar->height - h : 0);
  return {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

int touchbar_rotation_deg(int orient) { return valid(orient) ? orient * 90 : 0; }

int touchbar_orient_of_rotation(int deg) {
  deg %= 360;
  if (deg < 0) deg += 360;
  return deg % 90 == 0 ? deg / 90 : APP_ORIENT_BAR_BOTTOM;
}
