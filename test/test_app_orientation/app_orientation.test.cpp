#include <app_orientation.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_landscape_image() { TEST_ASSERT_EQUAL(APP_ORIENT_LANDSCAPE, app_orientation(1872, 1404, 1872, 1404)); }

void test_portrait_image_has_the_sides_swapped() {
  TEST_ASSERT_EQUAL(APP_ORIENT_PORTRAIT, app_orientation(1404, 1872, 1872, 1404));
}

void test_no_image_yet_is_unknown() {
  TEST_ASSERT_EQUAL(APP_ENOENT, app_orientation(0, 0, 1872, 1404));
  TEST_ASSERT_EQUAL(APP_ENOENT, app_orientation(-1, 1404, 1872, 1404));
}

void test_other_size_is_unknown() {
  TEST_ASSERT_EQUAL(APP_ENOENT, app_orientation(800, 480, 1872, 1404)); // a smaller image, drawn uncentred
  TEST_ASSERT_EQUAL(APP_ENOENT, app_orientation(1404, 1404, 1872, 1404));
}

void test_square_panel_is_never_portrait() { TEST_ASSERT_EQUAL(APP_ORIENT_LANDSCAPE, app_orientation(800, 800, 800, 800)); }

void test_the_call_stays_in_its_slot_when_the_table_grows() {
  TEST_ASSERT_EQUAL(APP_API_SLOT(35), offsetof(app_api_t, screen_orientation));
  TEST_ASSERT_EQUAL(APP_API_SLOT(36), offsetof(app_api_t, webshare_start_files)); // 2.3 (the Wi-Fi page's files profile)
  TEST_ASSERT_EQUAL(APP_API_SLOT(37), offsetof(app_api_t, espnow_start)); // 2.4 (ESP-NOW, touch hold)
  TEST_ASSERT_EQUAL(APP_API_SLOT(40), offsetof(app_api_t, touch_hold));
  TEST_ASSERT_EQUAL(APP_API_SLOT(41), offsetof(app_api_t, device_orientation)); // 2.5 (the accelerometer)
  TEST_ASSERT_EQUAL(APP_API_SLOT(42), offsetof(app_api_t, orientation_follow));
  TEST_ASSERT_EQUAL(APP_API_SLOT(43), offsetof(app_api_t, touchbar_layout));
  TEST_ASSERT_EQUAL(APP_API_SLOT(44), sizeof(app_api_t));
  TEST_ASSERT_EQUAL(5, APP_API_MINOR);
}

// API 2.5: screen_orientation keeps its two values, from the accelerometer once it has a reading
void test_old_call_maps_the_four_positions() {
  TEST_ASSERT_EQUAL(APP_ORIENT_LANDSCAPE, app_orientation_screen(APP_ORIENT_BAR_BOTTOM, APP_ORIENT_PORTRAIT));
  TEST_ASSERT_EQUAL(APP_ORIENT_LANDSCAPE, app_orientation_screen(APP_ORIENT_BAR_TOP, APP_ENOENT));
  TEST_ASSERT_EQUAL(APP_ORIENT_PORTRAIT, app_orientation_screen(APP_ORIENT_BAR_LEFT, APP_ORIENT_LANDSCAPE));
  TEST_ASSERT_EQUAL(APP_ORIENT_PORTRAIT, app_orientation_screen(APP_ORIENT_BAR_RIGHT, APP_ENOENT));
  TEST_ASSERT_EQUAL(APP_ENOENT, app_orientation_screen(-2, APP_ENOENT)); // no reading: TRMNL's image as before
  TEST_ASSERT_EQUAL(APP_ORIENT_PORTRAIT, app_orientation_screen(-2, APP_ORIENT_PORTRAIT));
}

void test_device_orientation_falls_back_to_the_image() {
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_RIGHT, app_orientation_device(APP_ORIENT_BAR_RIGHT, APP_ORIENT_LANDSCAPE));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_LEFT, app_orientation_device(-2, APP_ORIENT_PORTRAIT));
  TEST_ASSERT_EQUAL(APP_ORIENT_BAR_BOTTOM, app_orientation_device(-2, APP_ENOENT));
  TEST_ASSERT_EQUAL(APP_ORIENT_PORTRAIT, APP_ORIENT_BAR_LEFT); // the old portrait is the bar on the left
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_landscape_image);
  RUN_TEST(test_portrait_image_has_the_sides_swapped);
  RUN_TEST(test_no_image_yet_is_unknown);
  RUN_TEST(test_other_size_is_unknown);
  RUN_TEST(test_square_panel_is_never_portrait);
  RUN_TEST(test_the_call_stays_in_its_slot_when_the_table_grows);
  RUN_TEST(test_old_call_maps_the_four_positions);
  RUN_TEST(test_device_orientation_falls_back_to_the_image);
  return UNITY_END();
}
