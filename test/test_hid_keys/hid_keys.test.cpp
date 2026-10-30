#include <hid_keys.h>
#include <string.h>
#include <unity.h>

static hid_keys_t k;
static app_event_t ev[HID_KEYS_MAX_EVENTS];

void setUp() { hid_keys_init(&k); }
void tearDown() {}

static int feed(uint8_t mods, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0) {
  const uint8_t r[8] = {mods, 0, a, b, c, 0, 0, 0};
  return hid_keys_report(&k, r, sizeof(r), ev);
}

void test_press_and_release_pass_the_usage_without_a_layout() {
  TEST_ASSERT_EQUAL(1, feed(0, 0x14)); // Q on US, A on AZERTY: the app's to say
  TEST_ASSERT_EQUAL(APP_EVENT_KEY_DOWN, ev[0].type);
  TEST_ASSERT_EQUAL(APP_KEY_HID, ev[0].key);
  TEST_ASSERT_EQUAL_HEX8(0x14, ev[0].hid_usage);
  TEST_ASSERT_EQUAL_STRING("", ev[0].utf8);
  TEST_ASSERT_EQUAL(0, feed(0, 0x14)); // still held: nothing new
  TEST_ASSERT_EQUAL(1, feed(0));
  TEST_ASSERT_EQUAL(APP_EVENT_KEY_UP, ev[0].type);
  TEST_ASSERT_EQUAL_HEX8(0x14, ev[0].hid_usage);
}

void test_layout_independent_keys_get_their_code() {
  TEST_ASSERT_EQUAL(APP_KEY_ENTER, hid_keys_code(0x28));
  TEST_ASSERT_EQUAL(APP_KEY_BACKSPACE, hid_keys_code(0x2A));
  TEST_ASSERT_EQUAL(APP_KEY_UP, hid_keys_code(0x52));
  TEST_ASSERT_EQUAL(APP_KEY_F1, hid_keys_code(0x3A));
  TEST_ASSERT_EQUAL(APP_KEY_F1 + 11, hid_keys_code(0x45));
  TEST_ASSERT_EQUAL(APP_KEY_HID, hid_keys_code(0x64)); // the ISO key next to left Shift (< > on AZERTY)
}

void test_modifiers_left_right_and_altgr() {
  feed(0x02, 0x04); // left Shift + usage 4
  TEST_ASSERT_EQUAL_HEX8(APP_MOD_SHIFT, ev[0].modifiers);
  feed(0);
  feed(0x40, 0x1F); // AltGr + 2 (AZERTY: ~)
  TEST_ASSERT_EQUAL_HEX8(APP_MOD_ALTGR, ev[0].modifiers);
  TEST_ASSERT_EQUAL_HEX8(APP_MOD_CTRL | APP_MOD_ALT | APP_MOD_META | APP_MOD_SHIFT, hid_keys_mods(0x10 | 0x04 | 0x08 | 0x20));
}

void test_rollover_keeps_the_state() {
  feed(0, 0x04, 0x05);
  const uint8_t phantom[8] = {0, 0, 1, 1, 1, 1, 1, 1};
  TEST_ASSERT_EQUAL(0, hid_keys_report(&k, phantom, 8, ev));
  TEST_ASSERT_EQUAL(1, feed(0, 0x04)); // only 5 released, 4 was never let go
  TEST_ASSERT_EQUAL(APP_EVENT_KEY_UP, ev[0].type);
  TEST_ASSERT_EQUAL_HEX8(0x05, ev[0].hid_usage);
}

void test_short_or_missing_reports_are_ignored() {
  const uint8_t r[4] = {0, 0, 4, 0};
  TEST_ASSERT_EQUAL(0, hid_keys_report(&k, r, sizeof(r), ev));
  TEST_ASSERT_EQUAL(0, hid_keys_report(&k, nullptr, 8, ev));
  TEST_ASSERT_EQUAL(0, feed(0)); // nothing was taken as held
}

void test_six_keys_swapped_at_once_fit() {
  const uint8_t a[8] = {0, 0, 4, 5, 6, 7, 8, 9}, b[8] = {0, 0, 10, 11, 12, 13, 14, 15};
  TEST_ASSERT_EQUAL(6, hid_keys_report(&k, a, 8, ev));
  TEST_ASSERT_EQUAL(12, hid_keys_report(&k, b, 8, ev));
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(APP_EVENT_KEY_UP, ev[i].type);
  for (int i = 6; i < 12; i++) TEST_ASSERT_EQUAL(APP_EVENT_KEY_DOWN, ev[i].type);
}

void test_a_key_named_twice_is_one_press() {
  const uint8_t r[8] = {0, 0, 4, 4, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL(1, hid_keys_report(&k, r, 8, ev));
}

void test_keyboard_removed_mid_typing_releases_every_held_key() {
  feed(0x02, 0x04, 0x2C);
  TEST_ASSERT_EQUAL(2, hid_keys_release_all(&k, ev));
  TEST_ASSERT_EQUAL(APP_EVENT_KEY_UP, ev[0].type);
  TEST_ASSERT_EQUAL(APP_EVENT_KEY_UP, ev[1].type);
  TEST_ASSERT_EQUAL(0, hid_keys_release_all(&k, ev)); // twice: nothing more
  TEST_ASSERT_EQUAL(1, feed(0, 0x04)); // a new keyboard starts clean
  TEST_ASSERT_EQUAL(APP_EVENT_KEY_DOWN, ev[0].type);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_press_and_release_pass_the_usage_without_a_layout);
  RUN_TEST(test_layout_independent_keys_get_their_code);
  RUN_TEST(test_modifiers_left_right_and_altgr);
  RUN_TEST(test_rollover_keeps_the_state);
  RUN_TEST(test_short_or_missing_reports_are_ignored);
  RUN_TEST(test_six_keys_swapped_at_once_fit);
  RUN_TEST(test_a_key_named_twice_is_one_press);
  RUN_TEST(test_keyboard_removed_mid_typing_releases_every_held_key);
  return UNITY_END();
}
