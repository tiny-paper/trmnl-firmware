#include <touch_inject.h>
#include <unity.h>

void setUp(void) { touch_inject_clear(); }
void tearDown(void) {}

static void test_buttons_become_the_touch_bars_events(void) {
  touch_inject_ev_t e[4];
  TEST_ASSERT_TRUE(touch_inject_button("left"));
  TEST_ASSERT_TRUE(touch_inject_button("middle"));
  TEST_ASSERT_TRUE(touch_inject_button("right"));
  TEST_ASSERT_TRUE(touch_inject_button("long"));
  TEST_ASSERT_EQUAL(4, touch_inject_take(e, 4));
  TEST_ASSERT_EQUAL(0, e[0].channel); TEST_ASSERT_EQUAL(1, e[0].gesture); // left tap
  TEST_ASSERT_EQUAL(1, e[1].channel); TEST_ASSERT_EQUAL(1, e[1].gesture);
  TEST_ASSERT_EQUAL(2, e[2].channel); TEST_ASSERT_EQUAL(1, e[2].gesture);
  TEST_ASSERT_EQUAL(1, e[3].channel); TEST_ASSERT_EQUAL(2, e[3].gesture); // middle long press
}

static void test_stop_server_is_a_long_press_of_the_middle(void) {
  touch_inject_ev_t e[1];
  TEST_ASSERT_TRUE(touch_inject_button("stop"));
  TEST_ASSERT_EQUAL(1, touch_inject_take(e, 1));
  TEST_ASSERT_EQUAL(1, e[0].channel); TEST_ASSERT_EQUAL(2, e[0].gesture);
}

static void test_bad_input_queues_nothing(void) {
  touch_inject_ev_t e[1];
  TEST_ASSERT_FALSE(touch_inject_button(nullptr));
  TEST_ASSERT_FALSE(touch_inject_button(""));
  TEST_ASSERT_FALSE(touch_inject_button("LEFT"));
  TEST_ASSERT_FALSE(touch_inject_button("left; drop"));
  TEST_ASSERT_FALSE(touch_inject_push(3, 1));
  TEST_ASSERT_FALSE(touch_inject_push(0, 3));
  TEST_ASSERT_EQUAL(0, touch_inject_take(e, 1));
}

static void test_a_full_queue_drops_new_events_and_keeps_old_ones(void) {
  for (int i = 0; i < TOUCH_INJECT_MAX; i++) TEST_ASSERT_TRUE(touch_inject_push(0, 1));
  TEST_ASSERT_FALSE(touch_inject_button("stop")); // dropped, no crash, no overwrite
  touch_inject_ev_t e[TOUCH_INJECT_MAX + 2];
  TEST_ASSERT_EQUAL(TOUCH_INJECT_MAX, touch_inject_take(e, TOUCH_INJECT_MAX + 2));
  for (int i = 0; i < TOUCH_INJECT_MAX; i++) TEST_ASSERT_EQUAL(1, e[i].gesture);
  TEST_ASSERT_TRUE(touch_inject_button("stop")); // room again, and the ring wraps
  TEST_ASSERT_EQUAL(1, touch_inject_take(e, 2));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_buttons_become_the_touch_bars_events);
  RUN_TEST(test_stop_server_is_a_long_press_of_the_middle);
  RUN_TEST(test_bad_input_queues_nothing);
  RUN_TEST(test_a_full_queue_drops_new_events_and_keeps_old_ones);
  return UNITY_END();
}
