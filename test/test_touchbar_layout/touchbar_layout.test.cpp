#include <app_api.h>
#include <touchbar_layout.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

#define W 1872
#define H 1404

static app_touchbar_t layout(int o) {
  app_touchbar_t b;
  TEST_ASSERT_EQUAL(0, touchbar_layout_fill(o, W, H, &b));
  return b;
}

static bool inside(app_rect_t r, int w, int h) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h; }

// The bar at the bottom: the panel's own frame, channel 0 left
void test_bottom_is_the_panels_frame() {
  app_touchbar_t b = layout(APP_ORIENT_BAR_BOTTOM);
  TEST_ASSERT_EQUAL(W, b.width);
  TEST_ASSERT_EQUAL(APP_EDGE_BOTTOM, b.edge);
  TEST_ASSERT_EQUAL(0, b.zone[APP_TOUCH_LEFT].x);
  TEST_ASSERT_EQUAL(H - APP_TOUCHBAR_DEPTH, b.zone[APP_TOUCH_LEFT].y);
  TEST_ASSERT_EQUAL(W / 3, b.zone[APP_TOUCH_LEFT].w);
  TEST_ASSERT_EQUAL(APP_TOUCHBAR_DEPTH, b.zone[APP_TOUCH_LEFT].h);
  for (int ch = 0; ch < 3; ch++) TEST_ASSERT_EQUAL(ch, touchbar_zone_of_channel(APP_ORIENT_BAR_BOTTOM, ch));
}

// Upside down: the user's left is the panel's channel 2; the middle stays the middle
void test_top_swaps_left_and_right() {
  TEST_ASSERT_EQUAL(APP_TOUCH_RIGHT, touchbar_zone_of_channel(APP_ORIENT_BAR_TOP, 0));
  TEST_ASSERT_EQUAL(APP_TOUCH_MIDDLE, touchbar_zone_of_channel(APP_ORIENT_BAR_TOP, 1));
  TEST_ASSERT_EQUAL(APP_TOUCH_LEFT, touchbar_zone_of_channel(APP_ORIENT_BAR_TOP, 2));
  app_touchbar_t b = layout(APP_ORIENT_BAR_TOP);
  TEST_ASSERT_EQUAL(0, b.zone[APP_TOUCH_LEFT].x);
  TEST_ASSERT_EQUAL(0, b.zone[APP_TOUCH_LEFT].y);
}

// Portrait: the zones run down the edge; APP_TOUCH_LEFT is the top one
void test_portrait_left_zone_is_on_top() {
  for (int o = APP_ORIENT_BAR_LEFT; o <= APP_ORIENT_BAR_RIGHT; o += 2) {
    app_touchbar_t b = layout(o);
    TEST_ASSERT_EQUAL(H, b.width);
    TEST_ASSERT_EQUAL(W, b.height);
    TEST_ASSERT_EQUAL(0, b.zone[APP_TOUCH_LEFT].y);
    TEST_ASSERT_TRUE(b.zone[APP_TOUCH_MIDDLE].y > b.zone[APP_TOUCH_LEFT].y);
    TEST_ASSERT_TRUE(b.zone[APP_TOUCH_RIGHT].y > b.zone[APP_TOUCH_MIDDLE].y);
    TEST_ASSERT_EQUAL(o == APP_ORIENT_BAR_LEFT ? 0 : H - APP_TOUCHBAR_DEPTH, b.zone[APP_TOUCH_LEFT].x);
    TEST_ASSERT_EQUAL(APP_TOUCHBAR_DEPTH, b.zone[APP_TOUCH_LEFT].w);
  }
}

// Each zone is the image of its channel's panel strip: every corner maps back with app_orient_to_fb
void test_zone_rects_are_the_channels_strips() {
  for (int o = 0; o < 4; o++) {
    app_touchbar_t b = layout(o);
    for (int ch = 0; ch < 3; ch++) {
      app_rect_t r = b.zone[touchbar_zone_of_channel(o, ch)];
      TEST_ASSERT_TRUE(inside(r, b.width, b.height));
      int cx = r.x + r.w / 2, cy = r.y + r.h / 2, fx, fy;
      app_orient_to_fb(o, W, H, cx, cy, &fx, &fy);
      TEST_ASSERT_TRUE(fy >= H - APP_TOUCHBAR_DEPTH); // on the bar's strip of the panel
      TEST_ASSERT_EQUAL(ch, fx * 3 / W); // under its channel
    }
  }
}

// The hint pills: inside the screen, next to the bar, each on its own zone, in all four positions
void test_hint_boxes_next_to_their_zones() {
  const int pw = 360, ph = 90, gap = 40;
  for (int o = 0; o < 4; o++) {
    app_touchbar_t b = layout(o);
    for (int z = 0; z < 3; z++) {
      app_rect_t p = touchbar_hint_box(&b, z, pw, ph, gap);
      app_rect_t r = b.zone[z];
      TEST_ASSERT_TRUE(inside(p, b.width, b.height));
      int cx = p.x + p.w / 2, cy = p.y + p.h / 2;
      if (b.edge == APP_EDGE_BOTTOM || b.edge == APP_EDGE_TOP) {
        TEST_ASSERT_TRUE(cx >= r.x && cx < r.x + r.w);
        TEST_ASSERT_EQUAL(b.edge == APP_EDGE_TOP ? gap : b.height - gap - ph, p.y);
      } else {
        TEST_ASSERT_TRUE(cy >= r.y && cy < r.y + r.h);
        TEST_ASSERT_EQUAL(b.edge == APP_EDGE_LEFT ? gap : b.width - gap - pw, p.x);
      }
    }
  }
}

void test_rotation_round_trip_and_bad_input() {
  for (int o = 0; o < 4; o++) TEST_ASSERT_EQUAL(o, touchbar_orient_of_rotation(touchbar_rotation_deg(o)));
  TEST_ASSERT_EQUAL(90, touchbar_rotation_deg(APP_ORIENT_BAR_LEFT)); // FastEPD 90: (x, y) -> (y, H - 1 - x)
  app_touchbar_t b;
  TEST_ASSERT_EQUAL(APP_EINVAL, touchbar_layout_fill(4, W, H, &b));
  TEST_ASSERT_EQUAL(APP_EINVAL, touchbar_layout_fill(-1, W, H, &b));
  TEST_ASSERT_EQUAL(APP_EINVAL, touchbar_layout_fill(0, W, H, nullptr));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_bottom_is_the_panels_frame);
  RUN_TEST(test_top_swaps_left_and_right);
  RUN_TEST(test_portrait_left_zone_is_on_top);
  RUN_TEST(test_zone_rects_are_the_channels_strips);
  RUN_TEST(test_hint_boxes_next_to_their_zones);
  RUN_TEST(test_rotation_round_trip_and_bad_input);
  return UNITY_END();
}
