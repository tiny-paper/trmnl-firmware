#include <hid_report_map.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// A keyboard with report IDs as most Bluetooth keyboards have: keyboard 1 (modifiers, reserved, LEDs out,
// 6 keys), then a consumer control collection with report 3
static const uint8_t kbd_ids[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25,
    0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75,
    0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00, 0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x75, 0x10, 0x95, 0x02, 0x15, 0x01, 0x26, 0xFF, 0x02,
    0x19, 0x01, 0x2A, 0xFF, 0x02, 0x81, 0x00, 0xC0};

// N-key rollover: modifiers, then one bit per usage 0x00..0x77, no report ID
static const uint8_t kbd_bitmap[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
                                     0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x19, 0x00,
                                     0x29, 0x77, 0x95, 0x78, 0x75, 0x01, 0x81, 0x02, 0xC0};

// A mouse (report 2) before the keyboard (report 1): offsets are per report ID
static const uint8_t mouse[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
                                0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
                                0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01,
                                0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02,
                                0x81, 0x06, 0xC0, 0xC0};

void test_keyboard_with_report_ids() {
  hid_map_kbd_t k;
  TEST_ASSERT_TRUE(hid_map_parse(kbd_ids, sizeof(kbd_ids), &k));
  TEST_ASSERT_EQUAL(1, k.report_id);
  TEST_ASSERT_EQUAL(0, k.mod_bit);
  TEST_ASSERT_EQUAL(16, k.keys_bit); // after the reserved byte; the LED output does not count
  TEST_ASSERT_EQUAL(6, k.keys_count);
  TEST_ASSERT_EQUAL(-1, k.bitmap_bit);
  TEST_ASSERT_EQUAL(64, k.report_bits);
  const uint8_t r[8] = {0x02, 0, 0x04, 0x05, 0, 0, 0, 0}, want[8] = {0x02, 0, 0x04, 0x05, 0, 0, 0, 0};
  uint8_t boot[8];
  TEST_ASSERT_TRUE(hid_map_to_boot(&k, r, sizeof(r), boot));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, boot, 8);
  TEST_ASSERT_FALSE(hid_map_to_boot(&k, r, 7, boot)); // shorter than the layout
}

void test_bitmap_keyboard_to_boot_and_rollover() {
  hid_map_kbd_t k;
  TEST_ASSERT_TRUE(hid_map_parse(kbd_bitmap, sizeof(kbd_bitmap), &k));
  TEST_ASSERT_EQUAL(0, k.report_id);
  TEST_ASSERT_EQUAL(8, k.bitmap_bit);
  TEST_ASSERT_EQUAL(0, k.bitmap_min);
  TEST_ASSERT_EQUAL(0x78, k.bitmap_count);
  TEST_ASSERT_EQUAL(-1, k.keys_bit);
  TEST_ASSERT_EQUAL(128, k.report_bits);
  uint8_t r[16] = {0x40}; // right Alt
  r[1 + 0x04 / 8] |= 1 << (0x04 % 8); // a
  r[1 + 0x1F / 8] |= 1 << (0x1F % 8); // 2
  uint8_t boot[8];
  TEST_ASSERT_TRUE(hid_map_to_boot(&k, r, sizeof(r), boot));
  const uint8_t want[8] = {0x40, 0, 0x04, 0x1F, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, boot, 8);
  for (int u = 0x04; u < 0x0B; u++) r[1 + u / 8] |= 1 << (u % 8); // 7 more keys at once
  TEST_ASSERT_TRUE(hid_map_to_boot(&k, r, sizeof(r), boot));
  for (int i = 2; i < 8; i++) TEST_ASSERT_EQUAL_HEX8(0x01, boot[i]); // ErrorRollOver, as a boot keyboard
}

void test_mouse_only_is_not_a_keyboard() {
  hid_map_kbd_t k;
  TEST_ASSERT_FALSE(hid_map_parse(mouse, sizeof(mouse), &k));
  TEST_ASSERT_FALSE(k.found);
  uint8_t boot[8], r[8] = {};
  TEST_ASSERT_FALSE(hid_map_to_boot(&k, r, sizeof(r), boot));
}

void test_mouse_then_keyboard_counts_bits_per_report() {
  uint8_t map[sizeof(mouse) + sizeof(kbd_ids)];
  memcpy(map, mouse, sizeof(mouse));
  memcpy(map + sizeof(mouse), kbd_ids, sizeof(kbd_ids));
  hid_map_kbd_t k;
  TEST_ASSERT_TRUE(hid_map_parse(map, sizeof(map), &k));
  TEST_ASSERT_EQUAL(1, k.report_id);
  TEST_ASSERT_EQUAL(16, k.keys_bit);
}

void test_cut_off_or_broken_maps_are_refused() {
  hid_map_kbd_t k;
  for (size_t n = 0; n < sizeof(kbd_ids); n++) {
    bool ok = hid_map_parse(kbd_ids, n, &k);
    if (ok) TEST_ASSERT_TRUE(n >= 66); // only once the key array item is complete
  }
  TEST_ASSERT_FALSE(hid_map_parse(nullptr, 10, &k));
  const uint8_t id0[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x00}; // report ID 0 is not allowed
  TEST_ASSERT_FALSE(hid_map_parse(id0, sizeof(id0), &k));
  const uint8_t huge[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x75, 0x08,
                          0x96, 0xFF, 0x7F, 0x81, 0x00, 0xC0}; // 32767 keys
  TEST_ASSERT_FALSE(hid_map_parse(huge, sizeof(huge), &k));
  uint8_t noise[300];
  uint32_t x = 12345;
  for (int round = 0; round < 2000; round++) { // random bytes: never a crash or a read past the end
    for (size_t i = 0; i < sizeof(noise); i++) {
      x = x * 1103515245 + 12345;
      noise[i] = (uint8_t)(x >> 16);
    }
    if (hid_map_parse(noise, sizeof(noise), &k)) {
      uint8_t boot[8];
      hid_map_to_boot(&k, noise, sizeof(noise), boot);
    }
  }
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_keyboard_with_report_ids);
  RUN_TEST(test_bitmap_keyboard_to_boot_and_rollover);
  RUN_TEST(test_mouse_only_is_not_a_keyboard);
  RUN_TEST(test_mouse_then_keyboard_counts_bits_per_report);
  RUN_TEST(test_cut_off_or_broken_maps_are_refused);
  return UNITY_END();
}
