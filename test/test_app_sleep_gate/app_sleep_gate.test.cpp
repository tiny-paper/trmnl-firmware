#include <app_sleep_gate.h>
#include <unity.h>

void setUp() { app_sleep_gate_app_ended(); }
void tearDown() {}

void test_sleeps_when_no_app_runs() { TEST_ASSERT_EQUAL(APP_SLEEP_GO, app_sleep_gate_ask("refresh")); }

void test_sleep_refused_while_an_app_runs() {
  app_sleep_gate_app_started("writer");
  TEST_ASSERT_EQUAL(APP_SLEEP_REFUSED, app_sleep_gate_ask("refresh"));
  TEST_ASSERT_EQUAL(APP_SLEEP_REFUSED, app_sleep_gate_ask("restart"));
  TEST_ASSERT_EQUAL_STRING("writer", app_sleep_gate_app());
}

void test_sleeps_again_once_the_app_ended() {
  app_sleep_gate_app_started("writer");
  app_sleep_gate_app_ended();
  TEST_ASSERT_EQUAL(APP_SLEEP_GO, app_sleep_gate_ask("app ended"));
  TEST_ASSERT_NULL(app_sleep_gate_app());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sleeps_when_no_app_runs);
  RUN_TEST(test_sleep_refused_while_an_app_runs);
  RUN_TEST(test_sleeps_again_once_the_app_ended);
  return UNITY_END();
}
