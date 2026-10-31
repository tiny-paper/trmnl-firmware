#include <app_api.h>
#include <touch_hold.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

static const uint32_t MID = 1u << APP_TOUCH_MIDDLE;

// The app asked for the middle: from 600 ms a hold, shorter a tap
void test_middle_hold_from_600_ms() {
  TEST_ASSERT_EQUAL(TOUCH_RELEASE_TAP, touch_release_gesture(1, 599, MID));
  TEST_ASSERT_EQUAL(TOUCH_RELEASE_HOLD, touch_release_gesture(1, 600, MID));
  TEST_ASSERT_EQUAL(TOUCH_RELEASE_HOLD, touch_release_gesture(1, 1499, MID));
}

// No app asked (the menu, older apps): a slow press stays a tap, as before API 2.4
void test_not_asked_every_release_is_a_tap() {
  TEST_ASSERT_EQUAL(TOUCH_RELEASE_TAP, touch_release_gesture(1, 1000, 0));
}

// Left and right never hold, even when asked (only the middle is offered)
void test_left_and_right_stay_taps() {
  TEST_ASSERT_EQUAL(TOUCH_RELEASE_TAP, touch_release_gesture(0, 1000, 0x7));
  TEST_ASSERT_EQUAL(TOUCH_RELEASE_TAP, touch_release_gesture(2, 1000, 0x7));
  TEST_ASSERT_EQUAL(TOUCH_RELEASE_TAP, touch_release_gesture(5, 1000, 0x7));
}

// The values are touch_gesture_t's and the web page's (touch_inject.h: 0 down, 1 tap, 2 long)
void test_values_follow_touch_gesture_t() {
  TEST_ASSERT_EQUAL(1, TOUCH_RELEASE_TAP);
  TEST_ASSERT_EQUAL(3, TOUCH_RELEASE_HOLD);
  TEST_ASSERT_EQUAL(APP_TOUCH_HOLD_MS, TOUCH_HOLD_MS);
  TEST_ASSERT_EQUAL(MID, TOUCH_HOLD_ZONES);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_middle_hold_from_600_ms);
  RUN_TEST(test_not_asked_every_release_is_a_tap);
  RUN_TEST(test_left_and_right_stay_taps);
  RUN_TEST(test_values_follow_touch_gesture_t);
  return UNITY_END();
}
