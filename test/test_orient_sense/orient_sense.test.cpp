#include <app_api.h>
#include <math.h>
#include <orient_sense.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

#define G 2048 // about 1 g in the 4 g range

typedef struct {
  int32_t x, y, z;
} acc_t;

// A sample whose mount-corrected angle is `deg`, tilted `tilt` degrees up from lying flat (90: upright)
static acc_t at(float deg, float tilt) {
  float phi = (deg - ORIENT_MOUNT_OFFSET_DEG) * (float)M_PI / 180.0f, t = tilt * (float)M_PI / 180.0f;
  float horiz = G * sinf(t);
  return {(int32_t)lroundf(horiz * cosf(phi)), (int32_t)lroundf(-horiz * sinf(phi)), (int32_t)lroundf(G * cosf(t))};
}

static int classify(float deg, float tilt, int current) {
  acc_t a = at(deg, tilt);
  return orient_classify(a.x, a.y, a.z, current);
}

// TRMNL's sectors, named by where the touch bar is (rotation 0 is LANDSCAPE_RIGHT, the bar at the bottom)
void test_each_sector_names_where_the_bar_is() {
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, classify(180, 90, ORIENT_NONE));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_LEFT, classify(270, 90, ORIENT_NONE));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_TOP, classify(0, 90, ORIENT_NONE));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_RIGHT, classify(90, 90, ORIENT_NONE));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, classify(224, 90, ORIENT_NONE));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_LEFT, classify(226, 90, ORIENT_NONE));
}

// The same raw sample as TRMNL's bma530_compute_orientation: phi = atan2(-ay, ax), + 45
void test_angle_is_trmnls() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, orient_corrected_deg(G, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 135.0f, orient_corrected_deg(0, -G));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 315.0f, orient_corrected_deg(0, G));
}

void test_flat_and_nearly_flat_are_flat() {
  TEST_ASSERT_EQUAL(ORIENT_FLAT, classify(90, 0, APP_ORIENT_BAR_BOTTOM));
  TEST_ASSERT_EQUAL(ORIENT_FLAT, classify(90, 30, APP_ORIENT_BAR_BOTTOM));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_RIGHT, classify(90, 50, APP_ORIENT_BAR_BOTTOM)); // on a stand
  TEST_ASSERT_TRUE(orient_is_flat(0, 0, -G)); // face down as well
}

// Near a sector's edge the position it is in stays until ORIENT_HYSTERESIS_DEG past the edge
void test_hysteresis_at_the_edge() {
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, classify(235, 90, APP_ORIENT_BAR_BOTTOM));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, classify(239, 90, APP_ORIENT_BAR_BOTTOM));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_LEFT, classify(241, 90, APP_ORIENT_BAR_BOTTOM));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_LEFT, classify(215, 90, APP_ORIENT_BAR_LEFT)); // and back the same way
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, classify(209, 90, APP_ORIENT_BAR_LEFT));
}

void test_first_reading_counts_at_once() {
  orient_filter_t f;
  orient_filter_init(&f, 1000);
  TEST_ASSERT_FALSE(orient_filter_feed(&f, ORIENT_FLAT, 0));
  TEST_ASSERT_EQUAL(ORIENT_NONE, f.steady);
  TEST_ASSERT_TRUE(orient_filter_feed(&f, APP_ORIENT_BAR_LEFT, 100));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_LEFT, f.steady);
}

void test_a_new_position_counts_after_the_settle_time() {
  orient_filter_t f;
  orient_filter_init(&f, 1000);
  orient_filter_feed(&f, APP_ORIENT_BAR_BOTTOM, 0);
  for (uint32_t t = 100; t < 1100; t += 100) TEST_ASSERT_FALSE(orient_filter_feed(&f, APP_ORIENT_BAR_RIGHT, t));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, f.steady);
  TEST_ASSERT_TRUE(orient_filter_feed(&f, APP_ORIENT_BAR_RIGHT, 1100));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_RIGHT, f.steady);
  TEST_ASSERT_FALSE(orient_filter_feed(&f, APP_ORIENT_BAR_RIGHT, 1200)); // once
}

// One noisy sample back, or flat in between, starts the wait again
void test_noise_and_flat_restart_the_wait() {
  orient_filter_t f;
  orient_filter_init(&f, 1000);
  orient_filter_feed(&f, APP_ORIENT_BAR_BOTTOM, 0);
  orient_filter_feed(&f, APP_ORIENT_BAR_TOP, 100);
  orient_filter_feed(&f, APP_ORIENT_BAR_BOTTOM, 600); // noise
  TEST_ASSERT_FALSE(orient_filter_feed(&f, APP_ORIENT_BAR_TOP, 700));
  TEST_ASSERT_FALSE(orient_filter_feed(&f, APP_ORIENT_BAR_TOP, 1200));
  TEST_ASSERT_FALSE(orient_filter_feed(&f, ORIENT_FLAT, 1300));
  TEST_ASSERT_FALSE(orient_filter_feed(&f, APP_ORIENT_BAR_TOP, 1400));
  TEST_ASSERT_FALSE(orient_filter_feed(&f, APP_ORIENT_BAR_TOP, 2300));
  TEST_ASSERT_TRUE(orient_filter_feed(&f, APP_ORIENT_BAR_TOP, 2400));
}

// Lying flat for a long time keeps the last position
void test_flat_keeps_the_last() {
  orient_filter_t f;
  orient_filter_init(&f, 1000);
  orient_filter_feed(&f, APP_ORIENT_BAR_RIGHT, 0);
  for (uint32_t t = 100; t < 60000; t += 100) TEST_ASSERT_FALSE(orient_filter_feed(&f, ORIENT_FLAT, t));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_RIGHT, f.steady);
}

// A wobble across a sector edge (samples through classify with hysteresis) never flips
void test_wobble_at_an_edge_never_flips() {
  orient_filter_t f;
  orient_filter_init(&f, 1000);
  orient_filter_feed(&f, classify(180, 90, ORIENT_NONE), 0);
  for (uint32_t t = 100; t < 20000; t += 100) {
    float deg = 225.0f + ((t / 100) % 2 ? 12.0f : -12.0f); // +-12 around the edge
    TEST_ASSERT_FALSE(orient_filter_feed(&f, classify(deg, 90, f.steady), t));
  }
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, f.steady);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_each_sector_names_where_the_bar_is);
  RUN_TEST(test_angle_is_trmnls);
  RUN_TEST(test_flat_and_nearly_flat_are_flat);
  RUN_TEST(test_hysteresis_at_the_edge);
  RUN_TEST(test_first_reading_counts_at_once);
  RUN_TEST(test_a_new_position_counts_after_the_settle_time);
  RUN_TEST(test_noise_and_flat_restart_the_wait);
  RUN_TEST(test_flat_keeps_the_last);
  RUN_TEST(test_wobble_at_an_edge_never_flips);
  return UNITY_END();
}
