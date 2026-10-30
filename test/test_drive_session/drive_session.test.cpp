// When the USB drive's changes are taken (drive_session.h)
#include <drive_session.h>
#include <unity.h>

static drive_session_t s;

void test_nothing_written_takes_nothing(void) {
  drive_session_reset(&s);
  TEST_ASSERT_FALSE(drive_session_take(&s, DRIVE_END_EJECTED, 100000));
}

void test_eject_takes(void) {
  drive_session_reset(&s);
  drive_session_wrote(&s, 1000);
  TEST_ASSERT_TRUE(drive_session_take(&s, DRIVE_END_EJECTED, 1001));
}

void test_pulled_during_a_copy_takes_nothing(void) {
  drive_session_reset(&s);
  drive_session_wrote(&s, 1000);
  drive_session_wrote(&s, 1500);
  TEST_ASSERT_FALSE(drive_session_take(&s, DRIVE_END_UNPLUGGED, 1600));
  TEST_ASSERT_FALSE(drive_session_take(&s, DRIVE_END_STOPPED, 1600));
  TEST_ASSERT_FALSE(drive_session_take(&s, DRIVE_END_UNPLUGGED, 1500 + DRIVE_QUIET_MS - 1));
}

void test_pulled_after_a_quiet_time_or_a_sync_takes(void) {
  drive_session_reset(&s);
  drive_session_wrote(&s, 1000);
  TEST_ASSERT_TRUE(drive_session_take(&s, DRIVE_END_UNPLUGGED, 1000 + DRIVE_QUIET_MS));
  drive_session_synced(&s);
  TEST_ASSERT_TRUE(drive_session_take(&s, DRIVE_END_UNPLUGGED, 1001));
  // a write after the sync needs its own
  drive_session_wrote(&s, 1100);
  TEST_ASSERT_FALSE(drive_session_take(&s, DRIVE_END_UNPLUGGED, 1101));
}

void test_millis_wrapping(void) {
  drive_session_reset(&s);
  drive_session_wrote(&s, 0xffffff00u);
  TEST_ASSERT_FALSE(drive_session_take(&s, DRIVE_END_UNPLUGGED, 0x10));
  TEST_ASSERT_TRUE(drive_session_take(&s, DRIVE_END_UNPLUGGED, DRIVE_QUIET_MS));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_nothing_written_takes_nothing);
  RUN_TEST(test_eject_takes);
  RUN_TEST(test_pulled_during_a_copy_takes_nothing);
  RUN_TEST(test_pulled_after_a_quiet_time_or_a_sync_takes);
  RUN_TEST(test_millis_wrapping);
  return UNITY_END();
}
