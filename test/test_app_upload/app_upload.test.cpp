#include <app_upload.h>
#include <string.h>
#include <unity.h>

static const uint64_t FREE = 4u * 1024u * 1024u;

void test_header_ok() {
  app_upload_header_t h;
  TEST_ASSERT_EQUAL(APP_UPLOAD_OK, app_upload_parse("TAPP PUT hello.elf 1234 0a1B2c3D", FREE, &h));
  TEST_ASSERT_EQUAL_STRING("/apps/hello.elf", h.path);
  TEST_ASSERT_EQUAL_UINT32(1234, h.size);
  TEST_ASSERT_EQUAL_HEX32(0x0a1b2c3d, h.crc);
}

void test_bad_headers() {
  app_upload_header_t h;
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP GET hello.elf 1 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP PUT hello.elf 12x 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP PUT hello.elf -1 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP PUT hello.elf 12", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP PUT hello.elf 12 xyz", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP PUT hello.elf 12 123456789", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP PUT hello.elf 99999999999 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse("TAPP PUT hello.elf 12 0 extra", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_HEADER, app_upload_parse(nullptr, FREE, &h));
}

void test_header_found_after_noise() {
  TEST_ASSERT_EQUAL_STRING("TAPP PUT a.elf 1 0", app_upload_find_header("TAPP PUT a.elf 1 0"));
  TEST_ASSERT_EQUAL_STRING("TAPP PUT a.elf 1 0", app_upload_find_header("\x1b[0m~~ rst:0x1 TAPP PUT a.elf 1 0"));
  TEST_ASSERT_NULL(app_upload_find_header("TAP PUT a.elf 1 0"));
  TEST_ASSERT_NULL(app_upload_find_header(""));
  TEST_ASSERT_NULL(app_upload_find_header(nullptr));
}

void test_got_text_names_the_bytes() {
  char out[APP_UPLOAD_LINE_MAX];
  app_upload_got_text("AB\x01", 32, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("got 41 42 01", out);
  app_upload_got_text("ABCDEF", 2, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("got 41 42", out);
  char tiny[8];
  app_upload_got_text("ABCDEF", 32, tiny, sizeof(tiny)); // a small buffer never overruns
  TEST_ASSERT_EQUAL_UINT(7, strlen(tiny));
  app_upload_got_text("", 32, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("got", out);
}

void test_bad_names() {
  app_upload_header_t h;
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_NAME, app_upload_parse("TAPP PUT ../x.elf 1 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_NAME, app_upload_parse("TAPP PUT a/b.elf 1 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_NAME, app_upload_parse("TAPP PUT hello.bin 1 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_NAME, app_upload_parse("TAPP PUT .upload.elf 1 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_NAME, app_upload_parse("TAPP PUT  1 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_NAME,
                    app_upload_parse("TAPP PUT a-name-far-too-long-for-an-app.elf 1 0", FREE, &h));
}

void test_sizes_and_space() {
  app_upload_header_t h;
  TEST_ASSERT_EQUAL(APP_UPLOAD_EMPTY, app_upload_parse("TAPP PUT a.elf 0 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_TOO_BIG, app_upload_parse("TAPP PUT a.elf 2097153 0", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_OK, app_upload_parse("TAPP PUT a.elf 2097152 0", FREE, &h));
  // the file system less the margin: a full one refuses, nothing is deleted to make room
  TEST_ASSERT_EQUAL(APP_UPLOAD_NO_SPACE, app_upload_parse("TAPP PUT a.elf 100 0", APP_UPLOAD_MARGIN + 99, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_OK, app_upload_parse("TAPP PUT a.elf 100 0", APP_UPLOAD_MARGIN + 100, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_NO_SPACE, app_upload_parse("TAPP PUT a.elf 1 0", 0, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_NO_SPACE, app_upload_parse("TAPP PUT a.elf 1 0", APP_UPLOAD_MARGIN - 1, &h));
}

void test_check() {
  app_upload_header_t h;
  TEST_ASSERT_EQUAL(APP_UPLOAD_OK, app_upload_parse("TAPP PUT a.elf 9 cbf43926", FREE, &h));
  TEST_ASSERT_EQUAL(APP_UPLOAD_OK, app_upload_check(&h, 9, 0xcbf43926));
  TEST_ASSERT_EQUAL(APP_UPLOAD_SHORT, app_upload_check(&h, 8, 0xcbf43926));
  TEST_ASSERT_EQUAL(APP_UPLOAD_BAD_CRC, app_upload_check(&h, 9, 0xcbf43927));
  TEST_ASSERT_EQUAL_STRING("bad crc", app_upload_result_str(APP_UPLOAD_BAD_CRC));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_header_ok);
  RUN_TEST(test_bad_headers);
  RUN_TEST(test_header_found_after_noise);
  RUN_TEST(test_got_text_names_the_bytes);
  RUN_TEST(test_bad_names);
  RUN_TEST(test_sizes_and_space);
  RUN_TEST(test_check);
  return UNITY_END();
}
