#include <app_serial.h>
#include <string.h>
#include <unity.h>

static app_serial_t s;
static char keys[512], lines[256];

void setUp() {
  app_serial_init(&s);
  keys[0] = lines[0] = '\0';
}
void tearDown() {}

// Feeds `data`; the bytes typed into the app go to keys, the other lines to lines ("|" after each)
static void feed(const char *data, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint8_t k;
    const char *l;
    app_serial_out_t o = app_serial_feed(&s, (uint8_t)data[i], &k, &l);
    if (o == APP_SERIAL_KEY) {
      size_t len = strlen(keys);
      keys[len] = (char)k;
      keys[len + 1] = '\0';
    } else if (o == APP_SERIAL_LINE) {
      strcat(lines, l);
      strcat(lines, "|");
    }
  }
}
#define FEED(lit) feed(lit, sizeof(lit) - 1)

// Device, writer on b0ff008: "TAPP EVLOG" sent to read the event log was typed into the document
void test_tapp_lines_never_type() {
  FEED("TAPP EVLOG\nTAPP EVLOG CLEAR\r\nTAPP NORESUME\n");
  TEST_ASSERT_EQUAL_STRING("", keys);
  TEST_ASSERT_EQUAL_STRING("TAPP EVLOG|TAPP EVLOG CLEAR|TAPP NORESUME|", lines);
}

void test_plain_bytes_never_type() {
  FEED("zq\x1b[A\r\nTKEX a\n");
  TEST_ASSERT_EQUAL_STRING("", keys);
}

void test_a_tkey_line_types_its_text_with_escapes() {
  FEED("TKEY Hello, world\\r\\nTab\\there \\\\ \\e[A \\x13\\x1bOP\n");
  TEST_ASSERT_EQUAL_STRING("Hello, world\r\nTab\there \\ \x1b[A \x13\x1bOP", keys);
  TEST_ASSERT_EQUAL_STRING("", lines);
}

void test_utf8_and_long_tkey_lines_pass() {
  char big[300] = "TKEY ";
  for (int i = 5; i < 299; i++) big[i] = 'x';
  big[299] = '\n';
  feed(big, sizeof(big));
  TEST_ASSERT_EQUAL(294, strlen(keys));
  keys[0] = '\0';
  FEED("TKEY \xc3\xa9\n");
  TEST_ASSERT_EQUAL_STRING("\xc3\xa9", keys);
}

void test_broken_escapes_type_nothing_and_a_line_end_resets() {
  FEED("TKEY a\\q\\xzzb\\x4\n");
  TEST_ASSERT_EQUAL_STRING("azb", keys); // \q nothing, \xz nothing (the z too), \x4 cut by the line end
  FEED("TAPP EVLOG\n");
  TEST_ASSERT_EQUAL_STRING("TAPP EVLOG|", lines);
}

void test_noise_before_a_command_keeps_the_end_of_the_line() {
  FEED("\x01\x02garbage garbage garbage TAPP EVLOG\n");
  TEST_ASSERT_EQUAL_STRING("", keys);
  TEST_ASSERT_NOT_NULL(strstr(lines, "TAPP EVLOG|"));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_tapp_lines_never_type);
  RUN_TEST(test_plain_bytes_never_type);
  RUN_TEST(test_a_tkey_line_types_its_text_with_escapes);
  RUN_TEST(test_utf8_and_long_tkey_lines_pass);
  RUN_TEST(test_broken_escapes_type_nothing_and_a_line_end_resets);
  RUN_TEST(test_noise_before_a_command_keeps_the_end_of_the_line);
  return UNITY_END();
}
