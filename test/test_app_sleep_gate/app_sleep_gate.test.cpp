#include <app_sleep_gate.h>
#include <unity.h>

void setUp() { app_sleep_gate_app_ended(); app_sleep_gate_screen(nullptr); }
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

void test_sleep_refused_while_an_install_screen_is_on() {
  app_sleep_gate_screen("Install via Wi-Fi");
  TEST_ASSERT_EQUAL(APP_SLEEP_REFUSED, app_sleep_gate_ask("goToSleep"));
  TEST_ASSERT_EQUAL_STRING("Install via Wi-Fi", app_sleep_gate_screen_name());
  app_sleep_gate_screen(nullptr);
  TEST_ASSERT_EQUAL(APP_SLEEP_GO, app_sleep_gate_ask("goToSleep"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sleeps_when_no_app_runs);
  RUN_TEST(test_sleep_refused_while_an_app_runs);
  RUN_TEST(test_sleeps_again_once_the_app_ended);
  RUN_TEST(test_sleep_refused_while_an_install_screen_is_on);
  return UNITY_END();
}
