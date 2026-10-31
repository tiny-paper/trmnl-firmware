#include <kbd_layout.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

#define S APP_MOD_SHIFT
#define G APP_MOD_ALTGR

static uint32_t ch(int l, uint8_t usage, uint8_t mods = 0) { return kbd_layout_char(l, usage, mods); }

// The digit row, plain and with Shift, from 1 (0x1E) to 0 (0x27)
static void row(int l, const uint16_t plain[10], const uint16_t shift[10]) {
  for (int i = 0; i < 10; i++) {
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(plain[i], ch(l, 0x1E + i), kbd_layout_name(l));
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(shift[i], ch(l, 0x1E + i, S), kbd_layout_name(l));
  }
}

void test_belgian() {
  const uint16_t p[10] = {'&', 0xE9, '"', '\'', '(', 0xA7, 0xE8, '!', 0xE7, 0xE0}, s[10] = {'1', '2', '3', '4', '5',
                                                                                          '6', '7', '8', '9', '0'};
  row(KBD_LAYOUT_BE, p, s);
  TEST_ASSERT_EQUAL('a', ch(KBD_LAYOUT_BE, 0x14)); // the key labelled Q on US
  TEST_ASSERT_EQUAL('q', ch(KBD_LAYOUT_BE, 0x04));
  TEST_ASSERT_EQUAL('z', ch(KBD_LAYOUT_BE, 0x1A));
  TEST_ASSERT_EQUAL('W', ch(KBD_LAYOUT_BE, 0x1D, S));
  TEST_ASSERT_EQUAL('m', ch(KBD_LAYOUT_BE, 0x33));
  TEST_ASSERT_EQUAL(',', ch(KBD_LAYOUT_BE, 0x10));
  TEST_ASSERT_EQUAL('b', ch(KBD_LAYOUT_BE, 0x05));
  TEST_ASSERT_EQUAL(0xF9, ch(KBD_LAYOUT_BE, 0x34)); // u grave
  TEST_ASSERT_EQUAL('@', ch(KBD_LAYOUT_BE, 0x1F, G));
  TEST_ASSERT_EQUAL('#', ch(KBD_LAYOUT_BE, 0x20, G));
  TEST_ASSERT_EQUAL('~', ch(KBD_LAYOUT_BE, 0x38, G));
  TEST_ASSERT_EQUAL('\\', ch(KBD_LAYOUT_BE, 0x64, G));
  TEST_ASSERT_EQUAL(0x20AC, ch(KBD_LAYOUT_BE, 0x08, G)); // euro
  TEST_ASSERT_EQUAL('^', ch(KBD_LAYOUT_BE, 0x2F)); // dead key: its own accent
  TEST_ASSERT_EQUAL(0, ch(KBD_LAYOUT_BE, 0x05, G)); // AltGr+B: nothing
}

void test_french() {
  const uint16_t p[10] = {'&', 0xE9, '"', '\'', '(', '-', 0xE8, '_', 0xE7, 0xE0}, s[10] = {'1', '2', '3', '4', '5',
                                                                                        '6', '7', '8', '9', '0'};
  row(KBD_LAYOUT_FR, p, s);
  TEST_ASSERT_EQUAL('a', ch(KBD_LAYOUT_FR, 0x14));
  TEST_ASSERT_EQUAL('!', ch(KBD_LAYOUT_FR, 0x38)); // differs from Belgian '='
  TEST_ASSERT_EQUAL('=', ch(KBD_LAYOUT_FR, 0x2E));
  TEST_ASSERT_EQUAL('@', ch(KBD_LAYOUT_FR, 0x27, G));
  TEST_ASSERT_EQUAL('#', ch(KBD_LAYOUT_FR, 0x20, G));
  TEST_ASSERT_EQUAL('\\', ch(KBD_LAYOUT_FR, 0x25, G));
  TEST_ASSERT_EQUAL(0x20AC, ch(KBD_LAYOUT_FR, 0x08, G));
}

void test_us() {
  const uint16_t p[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}, s[10] = {'!', '@', '#', '$', '%',
                                                                                   '^', '&', '*', '(', ')'};
  row(KBD_LAYOUT_US, p, s);
  TEST_ASSERT_EQUAL('q', ch(KBD_LAYOUT_US, 0x14));
  TEST_ASSERT_EQUAL('A', ch(KBD_LAYOUT_US, 0x04, S));
  TEST_ASSERT_EQUAL('z', ch(KBD_LAYOUT_US, 0x1D));
  TEST_ASSERT_EQUAL(';', ch(KBD_LAYOUT_US, 0x33));
  TEST_ASSERT_EQUAL('"', ch(KBD_LAYOUT_US, 0x34, S));
  TEST_ASSERT_EQUAL('?', ch(KBD_LAYOUT_US, 0x38, S));
  TEST_ASSERT_EQUAL(0, ch(KBD_LAYOUT_US, 0x1F, G)); // no third level
}

void test_uk() {
  const uint16_t p[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}, s[10] = {'!', '"', 0xA3, '$', '%',
                                                                                   '^', '&', '*', '(', ')'};
  row(KBD_LAYOUT_UK, p, s);
  TEST_ASSERT_EQUAL('@', ch(KBD_LAYOUT_UK, 0x34, S));
  TEST_ASSERT_EQUAL('#', ch(KBD_LAYOUT_UK, 0x32));
  TEST_ASSERT_EQUAL('~', ch(KBD_LAYOUT_UK, 0x32, S));
  TEST_ASSERT_EQUAL(0x20AC, ch(KBD_LAYOUT_UK, 0x21, G));
  TEST_ASSERT_EQUAL('[', ch(KBD_LAYOUT_UK, 0x2F));
}

void test_german() {
  const uint16_t p[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}, s[10] = {'!', '"', 0xA7, '$', '%',
                                                                                   '&', '/', '(', ')', '='};
  row(KBD_LAYOUT_DE, p, s);
  TEST_ASSERT_EQUAL('z', ch(KBD_LAYOUT_DE, 0x1C)); // the key labelled Y on US
  TEST_ASSERT_EQUAL('y', ch(KBD_LAYOUT_DE, 0x1D));
  TEST_ASSERT_EQUAL(0xFC, ch(KBD_LAYOUT_DE, 0x2F)); // u umlaut
  TEST_ASSERT_EQUAL(0xD6, ch(KBD_LAYOUT_DE, 0x33, S));
  TEST_ASSERT_EQUAL(0xDF, ch(KBD_LAYOUT_DE, 0x2D)); // sharp s
  TEST_ASSERT_EQUAL('@', ch(KBD_LAYOUT_DE, 0x14, G));
  TEST_ASSERT_EQUAL('{', ch(KBD_LAYOUT_DE, 0x24, G));
  TEST_ASSERT_EQUAL('|', ch(KBD_LAYOUT_DE, 0x64, G));
  TEST_ASSERT_EQUAL(0x20AC, ch(KBD_LAYOUT_DE, 0x08, G));
}

void test_keypad_space_and_unknown() {
  for (int l = 0; l < KBD_LAYOUT_COUNT; l++) {
    TEST_ASSERT_EQUAL('1', ch(l, 0x59));
    TEST_ASSERT_EQUAL('0', ch(l, 0x62));
    TEST_ASSERT_EQUAL(' ', ch(l, 0x2C));
    TEST_ASSERT_EQUAL(0, ch(l, 0x58)); // keypad Enter: no character
    TEST_ASSERT_EQUAL(0, ch(l, 0x39)); // Caps Lock
  }
  TEST_ASSERT_EQUAL(0, ch(KBD_LAYOUT_COUNT, 0x04));
  TEST_ASSERT_EQUAL(0, ch(-1, 0x04));
}

void test_codes_and_names() {
  for (int l = 0; l < KBD_LAYOUT_COUNT; l++) TEST_ASSERT_EQUAL(l, kbd_layout_from_code(kbd_layout_code(l)));
  TEST_ASSERT_EQUAL(-1, kbd_layout_from_code("xx"));
  TEST_ASSERT_EQUAL(-1, kbd_layout_from_code(nullptr));
  TEST_ASSERT_EQUAL_STRING("Belgian AZERTY", kbd_layout_name(KBD_LAYOUT_DEFAULT));
  TEST_ASSERT_EQUAL_STRING("Belgian AZERTY", kbd_layout_name(99));
  TEST_ASSERT_EQUAL_STRING("German QWERTZ", kbd_layout_name(KBD_LAYOUT_DE));
}

void test_apply_fills_utf8_and_keeps_the_usage() {
  app_event_t ev = {};
  ev.type = APP_EVENT_KEY_DOWN;
  ev.key = APP_KEY_HID;
  ev.hid_usage = 0x1F; // 2 on US, e acute on Belgian
  TEST_ASSERT_TRUE(kbd_layout_apply(KBD_LAYOUT_BE, &ev));
  TEST_ASSERT_EQUAL(APP_KEY_CHAR, ev.key);
  TEST_ASSERT_EQUAL_STRING("\xC3\xA9", ev.utf8);
  TEST_ASSERT_EQUAL_HEX8(0x1F, ev.hid_usage);

  app_event_t eu = {};
  eu.type = APP_EVENT_KEY_DOWN;
  eu.key = APP_KEY_HID;
  eu.modifiers = G;
  eu.hid_usage = 0x08;
  TEST_ASSERT_TRUE(kbd_layout_apply(KBD_LAYOUT_DE, &eu));
  TEST_ASSERT_EQUAL_STRING("\xE2\x82\xAC", eu.utf8);

  app_event_t c = {}; // Ctrl+Z on AZERTY: the layout's z, Ctrl kept for the app
  c.type = APP_EVENT_KEY_DOWN;
  c.key = APP_KEY_HID;
  c.modifiers = APP_MOD_CTRL;
  c.hid_usage = 0x1A;
  TEST_ASSERT_TRUE(kbd_layout_apply(KBD_LAYOUT_BE, &c));
  TEST_ASSERT_EQUAL_STRING("z", c.utf8);
  TEST_ASSERT_EQUAL(APP_MOD_CTRL, c.modifiers);

  app_event_t f = {}; // a key without a character stays as it was
  f.type = APP_EVENT_KEY_DOWN;
  f.key = APP_KEY_HID;
  f.hid_usage = 0x39;
  TEST_ASSERT_FALSE(kbd_layout_apply(KBD_LAYOUT_BE, &f));
  TEST_ASSERT_EQUAL(APP_KEY_HID, f.key);
  TEST_ASSERT_EQUAL_STRING("", f.utf8);

  app_event_t e = {}; // Enter keeps its code
  e.type = APP_EVENT_KEY_DOWN;
  e.key = APP_KEY_ENTER;
  e.hid_usage = 0x28;
  TEST_ASSERT_FALSE(kbd_layout_apply(KBD_LAYOUT_BE, &e));
  TEST_ASSERT_EQUAL(APP_KEY_ENTER, e.key);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_belgian);
  RUN_TEST(test_french);
  RUN_TEST(test_us);
  RUN_TEST(test_uk);
  RUN_TEST(test_german);
  RUN_TEST(test_keypad_space_and_unknown);
  RUN_TEST(test_codes_and_names);
  RUN_TEST(test_apply_fills_utf8_and_keeps_the_usage);
  return UNITY_END();
}
