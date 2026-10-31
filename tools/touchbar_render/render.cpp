// Host render of the touch bar layout and hint pills in the four positions (touchbar_layout.h): one PGM per position,
// as the user sees the screen. Build and run: tools/touchbar_render/render.sh <out dir>
#include <stdio.h>
#include <string.h>
#include <touchbar_layout.h>

#define W 1872
#define H 1404
static unsigned char img[W * H];
static int iw, ih;

static void fill(app_rect_t r, unsigned char v) {
  for (int y = r.y; y < r.y + r.h; y++)
    for (int x = r.x; x < r.x + r.w; x++)
      if (x >= 0 && y >= 0 && x < iw && y < ih) img[y * iw + x] = v;
}

static void frame(app_rect_t r, int t, unsigned char v) {
  fill({r.x, r.y, r.w, (int16_t)t}, v);
  fill({r.x, (int16_t)(r.y + r.h - t), r.w, (int16_t)t}, v);
  fill({r.x, r.y, (int16_t)t, r.h}, v);
  fill({(int16_t)(r.x + r.w - t), r.y, (int16_t)t, r.h}, v);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  const char *names[4] = {"bar_bottom", "bar_left", "bar_top", "bar_right"};
  const int pill_w[3] = {380, 560, 300}, pill_h = 92; // "Previous", "Long press: stop" sized, "Next"
  for (int o = 0; o < 4; o++) {
    app_touchbar_t b;
    if (touchbar_layout_fill(o, W, H, &b) != 0) return 1;
    iw = b.width;
    ih = b.height;
    memset(img, 255, sizeof(img));
    frame({0, 0, (int16_t)iw, (int16_t)ih}, 4, 0);
    const unsigned char shade[3] = {200, 150, 200}; // the zones: left, middle, right
    for (int z = 0; z < 3; z++) fill(b.zone[z], shade[z]);
    for (int z = 0; z < 3; z++) {
      app_rect_t p = touchbar_hint_box(&b, z, pill_w[z], pill_h, 40);
      fill(p, 255);
      frame(p, 6, 0);
      // a mark inside the pill: one bar for the user's left, two for the middle, three for the right
      for (int k = 0; k <= z; k++) fill({(int16_t)(p.x + 30 + k * 30), (int16_t)(p.y + 20), 14, (int16_t)(pill_h - 40)}, 0);
    }
    // an upright arrow in the middle of the screen: what the user sees as up
    for (int i = 0; i < 120; i++) fill({(int16_t)(iw / 2 - i / 2), (int16_t)(ih / 2 - 200 + i), (int16_t)(i ? i : 1), 1}, 0);
    fill({(int16_t)(iw / 2 - 20), (int16_t)(ih / 2 - 80), 40, 200}, 0);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pgm", dir, names[o]);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P5\n%d %d\n255\n", iw, ih);
    fwrite(img, 1, (size_t)iw * ih, f);
    fclose(f);
    printf("%s: %dx%d edge %d zones L(%d,%d %dx%d) M(%d,%d) R(%d,%d)\n", names[o], iw, ih, b.edge, b.zone[0].x,
           b.zone[0].y, b.zone[0].w, b.zone[0].h, b.zone[1].x, b.zone[1].y, b.zone[2].x, b.zone[2].y);
  }
  return 0;
}
