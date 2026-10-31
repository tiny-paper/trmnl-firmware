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

void test_the_call_is_at_the_end_of_the_table() {
  TEST_ASSERT_EQUAL(APP_API_SLOT(35), offsetof(app_api_t, screen_orientation));
  TEST_ASSERT_EQUAL(APP_API_SLOT(36), sizeof(app_api_t));
  TEST_ASSERT_EQUAL(2, APP_API_MINOR);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_landscape_image);
  RUN_TEST(test_portrait_image_has_the_sides_swapped);
  RUN_TEST(test_no_image_yet_is_unknown);
  RUN_TEST(test_other_size_is_unknown);
  RUN_TEST(test_square_panel_is_never_portrait);
  RUN_TEST(test_the_call_is_at_the_end_of_the_table);
  return UNITY_END();
}
