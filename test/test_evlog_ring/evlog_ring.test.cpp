#include <evlog_ring.h>
#include <stdio.h>
#include <string.h>
#include <trmnl_crc32.h>
#include <unity.h>

static uint8_t data[EVLOG_DATA_BYTES], data2[EVLOG_DATA_BYTES];
static uint8_t img[EVLOG_BYTES];
static evlog_ring_t r;
static char line[EVLOG_LINE_MAX];

void setUp() { evlog_ring_init(&r, data); }
void tearDown() {}

static const char *at(const evlog_ring_t *ring, int i) {
  if (evlog_ring_line(ring, i, line, sizeof(line)) < 0) return "(none)";
  return line;
}

void test_empty_log() {
  TEST_ASSERT_EQUAL(0, evlog_ring_count(&r));
  TEST_ASSERT_EQUAL(-1, evlog_ring_line(&r, 0, line, sizeof(line)));
  char buf[16];
  TEST_ASSERT_EQUAL(0, evlog_ring_tail(&r, 5, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_lines_with_uptime_wall_clock_and_boot() {
  evlog_ring_new_boot(&r);
  evlog_ring_add(&r, 12345, 0, "BLE kbd: POP connected");
  evlog_ring_add(&r, 13000, 1793352131, "Apps: writer started"); // 2026-10-30 09:22:11 UTC
  evlog_ring_add(&r, 14000, 1000, "clock not set yet");
  TEST_ASSERT_EQUAL(3, evlog_ring_count(&r));
  TEST_ASSERT_EQUAL_STRING("#1 12.345s - BLE kbd: POP connected", at(&r, 0));
  TEST_ASSERT_EQUAL_STRING("#1 13.000s 2026-10-30 09:22:11Z Apps: writer started", at(&r, 1));
  TEST_ASSERT_EQUAL_STRING("#1 14.000s - clock not set yet", at(&r, 2));
}

void test_a_line_stays_one_line_and_is_cut() {
  char longer[300];
  memset(longer, 'a', sizeof(longer) - 1);
  longer[sizeof(longer) - 1] = '\0';
  evlog_ring_add(&r, 1, 0, "two\nlines\r\tx");
  evlog_ring_add(&r, 2, 0, longer);
  TEST_ASSERT_EQUAL_STRING("#0 0.001s - two lines  x", at(&r, 0));
  TEST_ASSERT_EQUAL((int)strlen("#0 0.002s - ") + EVLOG_TEXT_MAX, evlog_ring_line(&r, 1, line, sizeof(line)));
}

void test_the_same_event_counts_up() {
  for (int i = 0; i < 3; i++) evlog_ring_add(&r, 1000 + i, 0, "BLE kbd: connect failed");
  evlog_ring_add(&r, 2000, 0, "other");
  evlog_ring_add(&r, 3000, 0, "other");
  TEST_ASSERT_EQUAL(2, evlog_ring_count(&r));
  TEST_ASSERT_EQUAL_STRING("#0 1.002s - BLE kbd: connect failed (x3)", at(&r, 0));
  TEST_ASSERT_EQUAL_STRING("#0 3.000s - other (x2)", at(&r, 1));
  evlog_ring_new_boot(&r); // a new boot is a new line
  evlog_ring_add(&r, 10, 0, "other");
  TEST_ASSERT_EQUAL(3, evlog_ring_count(&r));
}

void test_wraps_and_stays_bounded() {
  char text[32];
  for (int i = 0; i < 5000; i++) {
    snprintf(text, sizeof(text), "event %d", i);
    evlog_ring_add(&r, (uint32_t)i, 0, text);
    TEST_ASSERT_TRUE(r.h.used <= r.h.cap);
  }
  int n = evlog_ring_count(&r);
  TEST_ASSERT_TRUE(n > 100);
  TEST_ASSERT_TRUE(n < 5000);
  TEST_ASSERT_EQUAL_STRING("#0 4.999s - event 4999", at(&r, n - 1)); // the newest kept
  snprintf(text, sizeof(text), "#0 %d.%03ds - event %d", (5000 - n) / 1000, (5000 - n) % 1000, 5000 - n);
  TEST_ASSERT_EQUAL_STRING(text, at(&r, 0)); // oldest first, in order
  TEST_ASSERT_EQUAL(EVLOG_BYTES, evlog_ring_save(&r, img, sizeof(img))); // the file never grows
}

void test_survives_a_restart() {
  evlog_ring_new_boot(&r);
  for (int i = 0; i < 2000; i++) evlog_ring_add(&r, (uint32_t)i, 0, i % 2 ? "USB kbd: removed" : "USB kbd: connected");
  evlog_ring_add(&r, 5000, 0, "USB kbd: boost could not be switched off - restarting");
  int before = evlog_ring_count(&r);
  TEST_ASSERT_EQUAL(EVLOG_BYTES, evlog_ring_save(&r, img, sizeof(img)));
  evlog_ring_t after;
  evlog_ring_init(&after, data2); // a restart: new RAM, the file read back
  TEST_ASSERT_TRUE(evlog_ring_load(&after, img, sizeof(img)));
  evlog_ring_new_boot(&after);
  evlog_ring_add(&after, 300, 0, "boot: software restart");
  int n = evlog_ring_count(&after); // full: the oldest made room
  TEST_ASSERT_TRUE(n >= before - 2 && n <= before);
  TEST_ASSERT_EQUAL_STRING("#1 5.000s - USB kbd: boost could not be switched off - restarting", at(&after, n - 2));
  TEST_ASSERT_EQUAL_STRING("#2 0.300s - boot: software restart", at(&after, n - 1));
}

void test_a_damaged_file_loads_as_an_empty_log() {
  evlog_ring_add(&r, 1, 0, "one");
  evlog_ring_add(&r, 2, 0, "two");
  evlog_ring_save(&r, img, sizeof(img));
  evlog_ring_t after;
  evlog_ring_init(&after, data2);
  TEST_ASSERT_FALSE(evlog_ring_load(&after, img, sizeof(img) - 1)); // cut
  TEST_ASSERT_FALSE(evlog_ring_load(&after, nullptr, 0));
  for (size_t i = 0; i < sizeof(img); i += 97) { // a flipped bit anywhere
    img[i] ^= 0x10;
    TEST_ASSERT_FALSE(evlog_ring_load(&after, img, sizeof(img)));
    TEST_ASSERT_EQUAL(0, evlog_ring_count(&after));
    img[i] ^= 0x10;
  }
  TEST_ASSERT_TRUE(evlog_ring_load(&after, img, sizeof(img)));
  TEST_ASSERT_EQUAL(2, evlog_ring_count(&after));
}

// A file written by a firmware with a ring bug: the CRC is right, the records are not
static void reseal(uint8_t *image) {
  evlog_hdr_t h;
  memcpy(&h, image, sizeof(h));
  h.crc = 0;
  h.crc = trmnl_crc32(trmnl_crc32(0, &h, sizeof(h)), image + sizeof(h), EVLOG_DATA_BYTES);
  memcpy(image, &h, sizeof(h));
}

void test_records_that_do_not_add_up_load_as_an_empty_log() {
  evlog_ring_add(&r, 1, 0, "one");
  evlog_ring_add(&r, 2, 0, "two");
  evlog_ring_t after;
  evlog_ring_init(&after, data2);
  evlog_ring_save(&r, img, sizeof(img));
  reseal(img);
  TEST_ASSERT_TRUE(evlog_ring_load(&after, img, sizeof(img))); // reseal alone changes nothing
  evlog_hdr_t *h = (evlog_hdr_t *)img;
  h->used += 3; // a record cut short at the end
  reseal(img);
  TEST_ASSERT_FALSE(evlog_ring_load(&after, img, sizeof(img)));
  evlog_ring_save(&r, img, sizeof(img));
  img[sizeof(evlog_hdr_t) + 11] = EVLOG_TEXT_MAX + 1; // a text length no record has
  reseal(img);
  TEST_ASSERT_FALSE(evlog_ring_load(&after, img, sizeof(img)));
  TEST_ASSERT_EQUAL(0, evlog_ring_count(&after));
}

void test_tail_gives_the_last_lines_that_fit() {
  evlog_ring_add(&r, 1000, 0, "a");
  evlog_ring_add(&r, 2000, 0, "b");
  evlog_ring_add(&r, 3000, 0, "c");
  char buf[200];
  TEST_ASSERT_EQUAL(2, evlog_ring_tail(&r, 2, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("#0 2.000s - b\n#0 3.000s - c\n", buf);
  TEST_ASSERT_EQUAL(3, evlog_ring_tail(&r, 99, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(1, evlog_ring_tail(&r, 3, buf, 16)); // room for one line: the newest
  TEST_ASSERT_EQUAL_STRING("#0 3.000s - c\n", buf);
  TEST_ASSERT_EQUAL(0, evlog_ring_tail(&r, 3, buf, 5));
  TEST_ASSERT_EQUAL_STRING("", buf);
  TEST_ASSERT_EQUAL(-1, evlog_ring_tail(&r, 3, nullptr, 10));
}

void test_clear_keeps_the_boot() {
  evlog_ring_new_boot(&r);
  evlog_ring_add(&r, 1, 0, "x");
  evlog_ring_clear(&r);
  TEST_ASSERT_EQUAL(0, evlog_ring_count(&r));
  TEST_ASSERT_TRUE(r.dirty);
  evlog_ring_add(&r, 2, 0, "y");
  TEST_ASSERT_EQUAL_STRING("#1 0.002s - y", at(&r, 0));
}

void test_flash_writes_are_rate_limited() {
  TEST_ASSERT_FALSE(evlog_ring_flush_due(&r, 0)); // nothing new: no write
  evlog_ring_add(&r, 10, 0, "first");
  TEST_ASSERT_TRUE(evlog_ring_flush_due(&r, 10)); // the first event goes out at once
  evlog_ring_flushed(&r, 10);
  int writes = 1;
  // an event every 100 ms for 10 minutes: one write per EVLOG_FLUSH_GAP_MS at most
  for (uint32_t t = 110; t < 600000; t += 100) {
    evlog_ring_add(&r, t, 0, t % 200 ? "BLE kbd: disconnected" : "BLE kbd: connected");
    if (evlog_ring_flush_due(&r, t)) {
      evlog_ring_flushed(&r, t);
      writes++;
    }
  }
  TEST_ASSERT_TRUE(writes <= 1 + 600000 / EVLOG_FLUSH_GAP_MS);
  TEST_ASSERT_TRUE(writes >= 600000 / EVLOG_FLUSH_GAP_MS - 1); // and events still reach the flash
  TEST_ASSERT_TRUE(evlog_ring_flush_due(&r, 0xFFFFFFF0u) || !r.dirty); // the clock's wrap does not stop it
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_empty_log);
  RUN_TEST(test_lines_with_uptime_wall_clock_and_boot);
  RUN_TEST(test_a_line_stays_one_line_and_is_cut);
  RUN_TEST(test_the_same_event_counts_up);
  RUN_TEST(test_wraps_and_stays_bounded);
  RUN_TEST(test_survives_a_restart);
  RUN_TEST(test_a_damaged_file_loads_as_an_empty_log);
  RUN_TEST(test_records_that_do_not_add_up_load_as_an_empty_log);
  RUN_TEST(test_tail_gives_the_last_lines_that_fit);
  RUN_TEST(test_clear_keeps_the_boot);
  RUN_TEST(test_flash_writes_are_rate_limited);
  return UNITY_END();
}
