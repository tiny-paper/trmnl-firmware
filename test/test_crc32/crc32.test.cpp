#include <trmnl_crc32.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_crc32_is_zlibs() {
  TEST_ASSERT_EQUAL_HEX32(0xcbf43926, trmnl_crc32(0, "123456789", 9)); // the standard check value
  uint32_t c = trmnl_crc32(0, "12345", 5);
  TEST_ASSERT_EQUAL_HEX32(0xcbf43926, trmnl_crc32(c, "6789", 4));
  TEST_ASSERT_EQUAL_HEX32(0, trmnl_crc32(0, "", 0));
  TEST_ASSERT_EQUAL_HEX32(0xd202ef8d, trmnl_crc32(0, "\0", 1));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_is_zlibs);
  return UNITY_END();
}
