#include <serial_watch.h>
#include <unity.h>

static serial_watch_t w;
static uint32_t t;

// Samples every SERIAL_WATCH_POLL_MS for `ms`; counts the kicks
static int run(uint32_t ms, int free, bool plugged, serial_watch_act_t *last) {
  int kicks = 0;
  for (uint32_t e = 0; e < ms; e += SERIAL_WATCH_POLL_MS) {
    t += SERIAL_WATCH_POLL_MS;
    serial_watch_act_t a = serial_watch_step(&w, t, free, plugged);
    if (a == SERIAL_WATCH_KICK) kicks++;
    if (last && a != SERIAL_WATCH_NONE) *last = a;
  }
  return kicks;
}

void setUp(void) {
  serial_watch_init(&w);
  t = 1000;
  serial_watch_step(&w, t, 4096, true); // empty ring seen first
}
void tearDown(void) {}

// The device's case: bytes wait, host there, nothing moves -> kicks, then the first room ends it with one RESUMED
void test_stuck_ring_with_host_is_kicked_until_it_moves(void) {
  TEST_ASSERT_EQUAL(0, run(SERIAL_WATCH_KICK_MS - SERIAL_WATCH_POLL_MS, 1000, true, nullptr));
  TEST_ASSERT_EQUAL(1, run(SERIAL_WATCH_POLL_MS, 1000, true, nullptr));
  TEST_ASSERT_EQUAL(4, run(4 * SERIAL_WATCH_KICK_MS, 1000, true, nullptr)); // one per KICK_MS, not per poll
  t += SERIAL_WATCH_POLL_MS;
  TEST_ASSERT_EQUAL(SERIAL_WATCH_RESUMED, serial_watch_step(&w, t, 1064, true));
  TEST_ASSERT_EQUAL_UINT32(5, w.resumed_kicks);
  TEST_ASSERT_EQUAL_UINT32(5 * SERIAL_WATCH_KICK_MS + SERIAL_WATCH_POLL_MS, w.stall_ms);
  t += SERIAL_WATCH_POLL_MS;
  TEST_ASSERT_EQUAL(SERIAL_WATCH_NONE, serial_watch_step(&w, t, 2000, true)); // once only
}

// Output that keeps draining is never kicked, also when it is busy all the time
void test_draining_ring_is_left_alone(void) {
  int free = 100;
  for (int i = 0; i < 40; i++) {
    t += SERIAL_WATCH_POLL_MS;
    TEST_ASSERT_EQUAL(SERIAL_WATCH_NONE, serial_watch_step(&w, t, free, true));
    free += 10;
  }
}

// An empty ring that stays empty is idle, not stalled
void test_idle_empty_ring_is_left_alone(void) { TEST_ASSERT_EQUAL(0, run(10000, 4096, true, nullptr)); }

// No host (no SOF): HWCDC dropping into a full ring is right, nothing to kick
void test_no_host_no_kick(void) { TEST_ASSERT_EQUAL(0, run(10000, 0, false, nullptr)); }

// A host that comes back while bytes still wait: the stall counts from its return, not from before
void test_stall_counts_from_host_return(void) {
  run(5000, 0, false, nullptr);
  TEST_ASSERT_EQUAL(0, run(SERIAL_WATCH_KICK_MS - SERIAL_WATCH_POLL_MS, 0, true, nullptr));
  TEST_ASSERT_EQUAL(1, run(SERIAL_WATCH_POLL_MS, 0, true, nullptr));
}

// The ring growing fuller (writes, nothing sent) is not progress
void test_filling_ring_is_a_stall(void) {
  int kicks = 0;
  int free = 4000;
  for (int i = 0; i < 8; i++) {
    t += SERIAL_WATCH_POLL_MS;
    if (serial_watch_step(&w, t, free, true) == SERIAL_WATCH_KICK) kicks++;
    free -= 100;
  }
  TEST_ASSERT_EQUAL(2, kicks);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_stuck_ring_with_host_is_kicked_until_it_moves);
  RUN_TEST(test_draining_ring_is_left_alone);
  RUN_TEST(test_idle_empty_ring_is_left_alone);
  RUN_TEST(test_no_host_no_kick);
  RUN_TEST(test_stall_counts_from_host_return);
  RUN_TEST(test_filling_ring_is_a_stall);
  return UNITY_END();
}
