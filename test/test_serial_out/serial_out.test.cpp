#include <serial_out.h>
#include <string.h>
#include <unity.h>

// A fake HWCDC TX ring: `cap` bytes, the host takes `drain` bytes per ms (0: not reading). A write beyond the free
// room drops the oldest queued bytes, as HWCDC's flushTXBuffer does while it judges the host gone.
typedef struct {
  char ring[64], got[1024];
  size_t cap, len, got_len, dropped, drain;
  uint32_t t;
} fake_t;

static fake_t f;

static int room(void *c) { return (int)(((fake_t *)c)->cap - ((fake_t *)c)->len); }
static size_t write_(void *c, const uint8_t *b, size_t n) {
  fake_t *k = (fake_t *)c;
  for (size_t i = 0; i < n; i++) {
    if (k->len == k->cap) { // full: the oldest byte goes
      memmove(k->ring, k->ring + 1, --k->len);
      k->dropped++;
    }
    k->ring[k->len++] = (char)b[i];
  }
  return n;
}
static uint32_t now_ms(void *c) { return ((fake_t *)c)->t; }
static void wait_ms(void *c, uint32_t ms) {
  fake_t *k = (fake_t *)c;
  for (uint32_t m = 0; m < ms; m++) {
    k->t++;
    size_t d = k->drain < k->len ? k->drain : k->len;
    memcpy(k->got + k->got_len, k->ring, d);
    k->got_len += d;
    memmove(k->ring, k->ring + d, k->len - d);
    k->len -= d;
  }
}
static const serial_out_port_t port = {room, write_, now_ms, wait_ms, &f};

void setUp() {
  memset(&f, 0, sizeof(f));
  f.cap = 16;
}
void tearDown() {}

static void drain_all(void) {
  f.drain = 64;
  wait_ms(&f, 1);
}

// A slow host (2 bytes per ms) and a ring much smaller than the answer: every byte arrives, in order
void test_slow_host_gets_every_byte(void) {
  const char *text = "TAPP EVLOG one\nTAPP EVLOG two\nTAPP EVLOG END\n";
  f.drain = 2;
  size_t n = serial_out_write(&port, (const uint8_t *)text, strlen(text), 100);
  drain_all();
  TEST_ASSERT_EQUAL(strlen(text), n);
  TEST_ASSERT_EQUAL(0, f.dropped);
  f.got[f.got_len] = '\0';
  TEST_ASSERT_EQUAL_STRING(text, f.got);
}

// The host stops reading: the write ends after the stall time, queued bytes are kept, none dropped
void test_host_not_reading_ends_after_stall_without_drops(void) {
  char text[100];
  memset(text, 'x', sizeof(text));
  f.drain = 0;
  size_t n = serial_out_write(&port, (const uint8_t *)text, sizeof(text), 50);
  TEST_ASSERT_EQUAL(16, n);
  TEST_ASSERT_EQUAL(0, f.dropped);
  TEST_ASSERT_UINT32_WITHIN(2, 50, f.t);
}

// Progress resets the stall time: a host that reads a byte every 30 ms is never given up with a 50 ms stall
void test_progress_resets_stall(void) {
  char text[40];
  memset(text, 'y', sizeof(text));
  // drain 1 byte every 30 ms: model with a custom loop
  f.drain = 0;
  size_t n = serial_out_write(&port, (const uint8_t *)text, 16, 50);
  TEST_ASSERT_EQUAL(16, n);
  uint32_t start = f.t;
  f.drain = 1;
  wait_ms(&f, 1); // one byte leaves
  f.drain = 0;
  n = serial_out_write(&port, (const uint8_t *)text, 1, 50);
  TEST_ASSERT_EQUAL(1, n);
  TEST_ASSERT_TRUE(f.t - start < 5); // the free byte was taken at once
  TEST_ASSERT_EQUAL(0, f.dropped);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_slow_host_gets_every_byte);
  RUN_TEST(test_host_not_reading_ends_after_stall_without_drops);
  RUN_TEST(test_progress_resets_stall);
  return UNITY_END();
}
