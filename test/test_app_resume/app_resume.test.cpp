#include <app_resume.h>
#include <memory_persistence.h>
#include <unity.h>

static char path[64];

void test_nothing_to_resume_at_first() {
  MemoryPersistence p;
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, false, path, sizeof(path)));
}

void test_resumes_an_app_that_started() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_GO, app_resume_take(p, false, path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("/apps/hello.elf", path);
}

// Taking it clears it: the resumed app has to start again before the next restart resumes it
void test_a_crash_while_starting_ends_the_resuming() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_GO, app_resume_take(p, false, path, sizeof(path)));
  app_resume_starting(p, path);
  app_resume_input(p);
  // ... crashes before APP_RESUME_STARTED_MS, the device restarts
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, false, path, sizeof(path)));
}

void test_an_app_that_never_started_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, false, path, sizeof(path)));
}

void test_a_panic_or_watchdog_reset_ends_the_resuming() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, true, path, sizeof(path)));
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, false, path, sizeof(path)));
}

void test_an_ended_app_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  app_resume_ended(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, false, path, sizeof(path)));
}

void test_a_path_too_long_for_the_buffer_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  char small[8];
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, false, small, sizeof(small)));
}

// Started, ran past APP_RESUME_STARTED_MS, but no touch or key ever reached it (a dead touch bar): not resumed,
// named so the screen can say so; the next restart does not try again
void test_an_app_no_input_reached_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/writer.elf");
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NO_INPUT, app_resume_take(p, false, path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("/apps/writer.elf", path);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, false, path, sizeof(path)));
}

// The input mark belongs to one run: a new start clears it
void test_input_of_an_earlier_run_does_not_count() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/writer.elf");
  app_resume_input(p);
  app_resume_ended(p);
  app_resume_starting(p, "/apps/writer.elf");
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NO_INPUT, app_resume_take(p, false, path, sizeof(path)));
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
  RUN_TEST(test_an_app_no_input_reached_is_not_resumed);
  RUN_TEST(test_input_of_an_earlier_run_does_not_count);
  return UNITY_END();
}
