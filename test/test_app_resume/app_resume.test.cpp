#include <app_resume.h>
#include <memory_persistence.h>
#include <unity.h>

static char path[64];

void test_nothing_to_resume_at_first() {
  MemoryPersistence p;
  TEST_ASSERT_FALSE(app_resume_take(p, false, path, sizeof(path)));
}

void test_resumes_an_app_that_started() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_started(p);
  TEST_ASSERT_TRUE(app_resume_take(p, false, path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("/apps/hello.elf", path);
}

// Taking it clears it: the resumed app has to start again before the next restart resumes it
void test_a_crash_while_starting_ends_the_resuming() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_started(p);
  TEST_ASSERT_TRUE(app_resume_take(p, false, path, sizeof(path)));
  app_resume_starting(p, path);
  // ... crashes before APP_RESUME_STARTED_MS, the device restarts
  TEST_ASSERT_FALSE(app_resume_take(p, false, path, sizeof(path)));
}

void test_an_app_that_never_started_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  TEST_ASSERT_FALSE(app_resume_take(p, false, path, sizeof(path)));
}

void test_a_panic_or_watchdog_reset_ends_the_resuming() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_started(p);
  TEST_ASSERT_FALSE(app_resume_take(p, true, path, sizeof(path)));
  TEST_ASSERT_FALSE(app_resume_take(p, false, path, sizeof(path)));
}

void test_an_ended_app_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_started(p);
  app_resume_ended(p);
  TEST_ASSERT_FALSE(app_resume_take(p, false, path, sizeof(path)));
}

void test_a_path_too_long_for_the_buffer_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_started(p);
  char small[8];
  TEST_ASSERT_FALSE(app_resume_take(p, false, small, sizeof(small)));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_nothing_to_resume_at_first);
  RUN_TEST(test_resumes_an_app_that_started);
  RUN_TEST(test_a_crash_while_starting_ends_the_resuming);
  RUN_TEST(test_an_app_that_never_started_is_not_resumed);
  RUN_TEST(test_a_panic_or_watchdog_reset_ends_the_resuming);
  RUN_TEST(test_an_ended_app_is_not_resumed);
  RUN_TEST(test_a_path_too_long_for_the_buffer_is_not_resumed);
  return UNITY_END();
}
