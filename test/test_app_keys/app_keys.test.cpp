#include <app_keys.h>
#include <string.h>
#include <unity.h>

#include <vector>

// The key events a byte string types
static std::vector<app_event_t> type(const char *bytes, bool idle = false) {
  app_key_decoder_t d;
  app_key_decoder_init(&d);
  std::vector<app_event_t> out;
  app_event_t ev;
  for (const char *b = bytes; *b; b++) {
    if (app_key_feed(&d, (uint8_t)*b, &ev)) out.push_back(ev);
  }
  if (idle && app_key_idle(&d, &ev)) out.push_back(ev);
  return out;
}

void test_text() {
  auto ev = type("hi");
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL(APP_EVENT_KEY_DOWN, ev[0].type);
  TEST_ASSERT_EQUAL(APP_KEY_CHAR, ev[0].key);
  TEST_ASSERT_EQUAL_STRING("h", ev[0].utf8);
  TEST_ASSERT_EQUAL_STRING("i", ev[1].utf8);
}

void test_utf8() {
  auto ev = type("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"); // e acute, euro, emoji
  TEST_ASSERT_EQUAL(3, ev.size());
  TEST_ASSERT_EQUAL_STRING("\xc3\xa9", ev[0].utf8);
  TEST_ASSERT_EQUAL_STRING("\xe2\x82\xac", ev[1].utf8);
  TEST_ASSERT_EQUAL_STRING("\xf0\x9f\x98\x80", ev[2].utf8);
}

void test_broken_utf8_is_dropped() {
  auto ev = type("\xc3x\x80y");
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL_STRING("x", ev[0].utf8);
  TEST_ASSERT_EQUAL_STRING("y", ev[1].utf8);
}

void test_control_keys() {
  auto ev = type("\r\t\x7f\x03");
  TEST_ASSERT_EQUAL(4, ev.size());
  TEST_ASSERT_EQUAL(APP_KEY_ENTER, ev[0].key);
  TEST_ASSERT_EQUAL(APP_KEY_TAB, ev[1].key);
  TEST_ASSERT_EQUAL(APP_KEY_BACKSPACE, ev[2].key);
  TEST_ASSERT_EQUAL(APP_KEY_CHAR, ev[3].key);
  TEST_ASSERT_EQUAL(APP_MOD_CTRL, ev[3].modifiers);
  TEST_ASSERT_EQUAL_STRING("c", ev[3].utf8);
}

void test_escape_sequences() {
  auto ev = type("\x1b[A\x1b[B\x1b[C\x1b[D\x1b[5~\x1b[3~\x1bOP");
  TEST_ASSERT_EQUAL(7, ev.size());
  TEST_ASSERT_EQUAL(APP_KEY_UP, ev[0].key);
  TEST_ASSERT_EQUAL(APP_KEY_DOWN, ev[1].key);
  TEST_ASSERT_EQUAL(APP_KEY_RIGHT, ev[2].key);
  TEST_ASSERT_EQUAL(APP_KEY_LEFT, ev[3].key);
  TEST_ASSERT_EQUAL(APP_KEY_PAGE_UP, ev[4].key);
  TEST_ASSERT_EQUAL(APP_KEY_DELETE, ev[5].key);
  TEST_ASSERT_EQUAL(APP_KEY_F1, ev[6].key);
  TEST_ASSERT_EQUAL_STRING("", ev[0].utf8);
}

void test_lone_escape_on_idle() {
  auto ev = type("\x1b", true);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(APP_KEY_ESCAPE, ev[0].key);
}

void test_unknown_or_long_sequences_are_dropped() {
  auto ev = type("\x1b[99999999999Ax\x1bZy");
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL_STRING("x", ev[0].utf8);
  TEST_ASSERT_EQUAL_STRING("y", ev[1].utf8);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_text);
  RUN_TEST(test_utf8);
  RUN_TEST(test_broken_utf8_is_dropped);
  RUN_TEST(test_control_keys);
  RUN_TEST(test_escape_sequences);
  RUN_TEST(test_lone_escape_on_idle);
  RUN_TEST(test_unknown_or_long_sequences_are_dropped);
  return UNITY_END();
}
