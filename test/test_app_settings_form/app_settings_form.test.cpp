#include <app_settings_form.h>
#include <string.h>
#include <unity.h>

static asf_form_t form;
static char why[ASF_WHY_MAX];

static const app_setting_decl_t GOOD[] = {
    {"name", "Pilot name", nullptr, 1, 16, APP_SETTING_TEXT},
    {"bind", "Bind phrase", nullptr, 0, 64, APP_SETTING_SECRET},
    {"channel", "Wi-Fi channel", nullptr, 1, 13, APP_SETTING_NUMBER},
    {"units", "Einheiten", "metrisch|imperial", 0, 0, APP_SETTING_CHOICE},
};

void setUp() {
  memset(&form, 0, sizeof(form));
  TEST_ASSERT_EQUAL(0, asf_declare(&form, GOOD, 4));
}
void tearDown() {}

static int check(const char *key, const char *v) {
  return asf_check(&form.f[asf_find(&form, key)], v, strlen(v), why, sizeof(why));
}

void test_good_values_pass() {
  TEST_ASSERT_EQUAL(4, form.n);
  TEST_ASSERT_EQUAL(0, check("name", "Ward"));
  TEST_ASSERT_EQUAL(0, check("name", "Ren\xC3\xA9")); // UTF-8
  TEST_ASSERT_EQUAL(0, check("bind", "secret phrase"));
  TEST_ASSERT_EQUAL(0, check("bind", "")); // min 0: clears
  TEST_ASSERT_EQUAL(0, check("channel", "13"));
  TEST_ASSERT_EQUAL(0, check("units", "imperial"));
}

void test_too_long_and_too_short() {
  TEST_ASSERT_EQUAL(APP_EINVAL, check("name", "seventeen letters"));
  TEST_ASSERT_EQUAL_STRING("too long (at most 16 bytes)", why);
  TEST_ASSERT_EQUAL(APP_EINVAL, check("name", ""));
  TEST_ASSERT_EQUAL_STRING("too short (at least 1)", why);
  TEST_ASSERT_EQUAL(APP_EINVAL, check("name", "a\nb"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("name", "\xC3")); // cut UTF-8
}

void test_bad_number() {
  TEST_ASSERT_EQUAL(APP_EINVAL, check("channel", "1a"));
  TEST_ASSERT_EQUAL_STRING("not a whole number", why);
  TEST_ASSERT_EQUAL(APP_EINVAL, check("channel", ""));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("channel", "-"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("channel", "+3"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("channel", "14"));
  TEST_ASSERT_EQUAL_STRING("at most 13", why);
  TEST_ASSERT_EQUAL(APP_EINVAL, check("channel", "0"));
  TEST_ASSERT_EQUAL_STRING("at least 1", why);
  TEST_ASSERT_EQUAL(APP_EINVAL, check("channel", "99999999999")); // would wrap a 32-bit number
}

void test_unknown_choice() {
  TEST_ASSERT_EQUAL(APP_EINVAL, check("units", "imperia"));
  TEST_ASSERT_EQUAL_STRING("not one of the choices", why);
  TEST_ASSERT_EQUAL(APP_EINVAL, check("units", "metrisch|imperial"));
  char c[32];
  TEST_ASSERT_TRUE(asf_choice(&form.f[3], 1, c, sizeof(c)));
  TEST_ASSERT_EQUAL_STRING("imperial", c);
  TEST_ASSERT_FALSE(asf_choice(&form.f[3], 2, c, sizeof(c)));
}

void test_secret_is_never_shown_or_echoed() {
  char out[ASF_VALUE_MAX + 1];
  bool set = false;
  TEST_ASSERT_EQUAL(0, asf_shown(&form.f[1], "hunter2", 7, out, sizeof(out), &set));
  TEST_ASSERT_TRUE(set);
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL(0, asf_shown(&form.f[1], nullptr, 0, out, sizeof(out), &set));
  TEST_ASSERT_FALSE(set);
  TEST_ASSERT_EQUAL(4, asf_shown(&form.f[0], "Ward", 4, out, sizeof(out), &set));
  TEST_ASSERT_EQUAL_STRING("Ward", out);
  // a refused secret's message never holds the value
  char big[80];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  TEST_ASSERT_EQUAL(APP_EINVAL, check("bind", big));
  TEST_ASSERT_NULL(strstr(why, "xxx"));
}

void test_bad_declarations_are_refused_and_keep_nothing() {
  const app_setting_decl_t bad[] = {
      {"", "Empty key", nullptr, 0, 8, APP_SETTING_TEXT},
      {"sixteen_letters_", "Long key", nullptr, 0, 8, APP_SETTING_TEXT},
      {"a-b", "Dash", nullptr, 0, 8, APP_SETTING_TEXT},
      {"k", "", nullptr, 0, 8, APP_SETTING_TEXT},
      {"k", "Label\n", nullptr, 0, 8, APP_SETTING_TEXT},
      {"k", "Too long text", nullptr, 0, 257, APP_SETTING_TEXT},
      {"k", "Zero max", nullptr, 0, 0, APP_SETTING_SECRET},
      {"k", "Min over max", nullptr, 5, 4, APP_SETTING_NUMBER},
      {"k", "No choices", nullptr, 0, 0, APP_SETTING_CHOICE},
      {"k", "Empty choice", "a||b", 0, 0, APP_SETTING_CHOICE},
      {"k", "Same twice", "a|b|a", 0, 0, APP_SETTING_CHOICE},
      {"k", "17 choices", "a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q", 0, 0, APP_SETTING_CHOICE},
      {"k", "Kind", nullptr, 0, 8, 9},
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT_EQUAL_MESSAGE(APP_EINVAL, asf_declare(&form, &bad[i], 1), bad[i].label);
    TEST_ASSERT_EQUAL(4, form.n); // the old form stays
  }
  const app_setting_decl_t twice[] = {GOOD[0], GOOD[0]};
  TEST_ASSERT_EQUAL(APP_EINVAL, asf_declare(&form, twice, 2));
  TEST_ASSERT_EQUAL(APP_EINVAL, asf_declare(&form, nullptr, 1));
  TEST_ASSERT_EQUAL(APP_EINVAL, asf_declare(&form, GOOD, ASF_FIELDS_MAX + 1));
  TEST_ASSERT_EQUAL(0, asf_declare(&form, nullptr, 0));
  TEST_ASSERT_EQUAL(0, form.n);
  TEST_ASSERT_EQUAL(-1, asf_find(&form, "name"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_good_values_pass);
  RUN_TEST(test_too_long_and_too_short);
  RUN_TEST(test_bad_number);
  RUN_TEST(test_unknown_choice);
  RUN_TEST(test_secret_is_never_shown_or_echoed);
  RUN_TEST(test_bad_declarations_are_refused_and_keep_nothing);
  return UNITY_END();
}
