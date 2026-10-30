// The kept handles of a bonded keyboard (ble_hogp.h hogp_cache_t), against a fake keyboard that answers each
// GATT request and records it
#include <ble_hogp.h>
#include <string.h>
#include <unity.h>

static hogp_t h;
static const uint8_t addr[7] = {0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

// Report map: keyboard report 1 (modifiers, reserved, 6 keys)
static const uint8_t kbd_map[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0,
                                  0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
                                  0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
                                  0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
                                  0xC0};

// The fake keyboard: HID service 0x10..0x30, map 0x12, input report 0x15 (CCCD 0x16, ref 0x17), protocol 0x1c.
// Service Changed of its GATT service at 0x03
struct fake_t {
  hogp_do_t calls[32];
  int n;
  int subscribe_status; // the answer to the CCCD write
} fake;

static int count(hogp_do_t what) {
  int c = 0;
  for (int i = 0; i < fake.n; i++) c += fake.calls[i] == what;
  return c;
}

// Carries out actions as NimBLE would, until there is none left; the last one
static hogp_act_t play(hogp_act_t a) {
  while (a.what != HOGP_DO_NONE && a.what != HOGP_DO_READY && a.what != HOGP_DO_FAIL && fake.n < 32) {
    fake.calls[fake.n++] = a.what;
    switch (a.what) {
    case HOGP_DO_FIND_SERVICE:
      hogp_service(&h, HOGP_UUID_HID_SERVICE, 0x10, 0x30);
      a = hogp_services_done(&h, 0);
      break;
    case HOGP_DO_FIND_CHRS:
      hogp_chr(&h, HOGP_UUID_REPORT_MAP, 0x12);
      hogp_chr(&h, HOGP_UUID_REPORT, 0x15);
      hogp_chr(&h, HOGP_UUID_PROTOCOL, 0x1c);
      a = hogp_chrs_done(&h, 0);
      break;
    case HOGP_DO_FIND_DSCS:
      hogp_dsc(&h, 0x16, HOGP_UUID_CCCD);
      hogp_dsc(&h, 0x17, HOGP_UUID_REPORT_REF);
      a = hogp_dscs_done(&h, 0);
      break;
    case HOGP_DO_READ_LONG:
      hogp_read_data(&h, kbd_map, sizeof(kbd_map));
      a = hogp_read_done(&h, 0);
      break;
    case HOGP_DO_READ: {
      const uint8_t ref[2] = {1, 1};
      hogp_read_data(&h, ref, 2);
      a = hogp_read_done(&h, 0);
      break;
    }
    case HOGP_DO_WRITE_NO_RSP: a = hogp_written(&h, 0); break;
    case HOGP_DO_WRITE: {
      int st = fake.subscribe_status;
      fake.subscribe_status = 0; // fails once
      a = hogp_written(&h, st);
      break;
    }
    default: a.what = HOGP_DO_NONE; break;
    }
  }
  return a;
}

static hogp_act_t connect(const hogp_cache_t *c) {
  TEST_ASSERT_EQUAL(HOGP_DO_SECURE, hogp_start_cached(&h, 0, c).what);
  return play(hogp_encrypted(&h, 0, 100));
}

static void typing() {
  const uint8_t r[8] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
  uint8_t boot[8];
  TEST_ASSERT_TRUE(hogp_report(&h, 0x15, r, sizeof(r), boot));
}

static hogp_cache_t first_connect() {
  hogp_cache_t c;
  TEST_ASSERT_EQUAL(HOGP_DO_READY, connect(nullptr).what);
  TEST_ASSERT_TRUE(hogp_cache_fill(&h, addr, &c));
  TEST_ASSERT_TRUE(hogp_cache_valid(&c, addr));
  return c;
}

void setUp() { memset(&fake, 0, sizeof(fake)); }
void tearDown() {}

void test_first_connect_discovers() {
  first_connect();
  TEST_ASSERT_EQUAL(1, count(HOGP_DO_FIND_SERVICE));
  TEST_ASSERT_EQUAL(1, count(HOGP_DO_READ_LONG));
  TEST_ASSERT_FALSE(h.from_cache);
}

// The 10 to 20 s: a reconnect with kept handles asks only protocol mode and the CCCD, then types
void test_reconnect_with_the_cache_skips_discovery_and_subscribes() {
  hogp_cache_t c = first_connect();
  memset(&fake, 0, sizeof(fake));
  TEST_ASSERT_EQUAL(HOGP_DO_READY, connect(&c).what);
  TEST_ASSERT_EQUAL(2, fake.n);
  TEST_ASSERT_EQUAL(HOGP_DO_WRITE_NO_RSP, fake.calls[0]); // protocol mode: report
  TEST_ASSERT_EQUAL(HOGP_DO_WRITE, fake.calls[1]); // the CCCD
  TEST_ASSERT_TRUE(h.from_cache);
  TEST_ASSERT_FALSE(h.cache_stale);
  typing();
}

void test_cache_is_not_used_for_another_keyboard_or_version() {
  hogp_cache_t c = first_connect();
  uint8_t other[7];
  memcpy(other, addr, 7);
  other[6] ^= 1;
  TEST_ASSERT_FALSE(hogp_cache_valid(&c, other));
  hogp_cache_t old = c;
  old.version = 0;
  TEST_ASSERT_FALSE(hogp_cache_valid(&old, addr));
  hogp_cache_t bad = c;
  bad.chrs = HOGP_MAX_CHRS + 1;
  TEST_ASSERT_FALSE(hogp_cache_valid(&bad, addr));
  bad = c;
  bad.chr[0].val = 0x40; // outside the service
  TEST_ASSERT_FALSE(hogp_cache_valid(&bad, addr));
  TEST_ASSERT_FALSE(hogp_cache_valid(nullptr, addr));
}

void test_failed_subscribe_drops_the_cache_and_discovers_on_the_same_link() {
  hogp_cache_t c = first_connect();
  memset(&fake, 0, sizeof(fake));
  fake.subscribe_status = HOGP_HS_ATT_BASE + 0x01; // invalid handle: the keyboard's handles moved
  TEST_ASSERT_EQUAL(HOGP_DO_READY, connect(&c).what);
  TEST_ASSERT_TRUE(h.cache_stale);
  TEST_ASSERT_FALSE(h.from_cache); // the caller keeps the new handles
  TEST_ASSERT_EQUAL(1, count(HOGP_DO_FIND_SERVICE));
  typing();
}

void test_service_changed_drops_the_cache_and_discovers_again() {
  hogp_cache_t c = first_connect();
  memset(&fake, 0, sizeof(fake));
  TEST_ASSERT_EQUAL(HOGP_DO_READY, connect(&c).what);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_indicated(&h, 0x15, 1000).what); // a HID value: not Service Changed
  TEST_ASSERT_FALSE(h.cache_stale);
  hogp_act_t a = hogp_indicated(&h, 0x03, 1000);
  TEST_ASSERT_EQUAL(HOGP_DO_FIND_SERVICE, a.what);
  TEST_ASSERT_TRUE(h.cache_stale);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_tick(&h, 1500).what); // a fresh deadline for the discovery
  TEST_ASSERT_EQUAL(HOGP_DO_READY, play(a).what);
  typing();
}

void test_indication_before_encryption_does_nothing() {
  hogp_start(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_indicated(&h, 0x03, 10).what);
  TEST_ASSERT_FALSE(h.cache_stale);
}

void test_cache_filled_only_when_ready() {
  hogp_cache_t c;
  hogp_start(&h, 0);
  TEST_ASSERT_FALSE(hogp_cache_fill(&h, addr, &c));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_first_connect_discovers);
  RUN_TEST(test_reconnect_with_the_cache_skips_discovery_and_subscribes);
  RUN_TEST(test_cache_is_not_used_for_another_keyboard_or_version);
  RUN_TEST(test_failed_subscribe_drops_the_cache_and_discovers_on_the_same_link);
  RUN_TEST(test_service_changed_drops_the_cache_and_discovers_again);
  RUN_TEST(test_indication_before_encryption_does_nothing);
  RUN_TEST(test_cache_filled_only_when_ready);
  return UNITY_END();
}
