#include <espnow_rx.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

static const uint8_t SRC[6] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};

// ---- MAC ----

// The ELRS backpack's rule: the UID with bit 0 of byte 0 cleared, the other bytes as they are
void test_mac_is_the_uid_with_the_multicast_bit_cleared() {
  const uint8_t uid[6] = {0x0b, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t mac[6];
  TEST_ASSERT_TRUE(espnow_mac_from_uid(uid, mac));
  const uint8_t want[6] = {0x0a, 0x22, 0x33, 0x44, 0x55, 0x66};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, mac, 6);
  const uint8_t even[6] = {0xfe, 1, 2, 3, 4, 5};
  TEST_ASSERT_TRUE(espnow_mac_from_uid(even, mac));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(even, mac, 6);
}

void test_all_zero_mac_refused() {
  const uint8_t uid[6] = {0x01, 0, 0, 0, 0, 0};
  uint8_t mac[6] = {9, 9, 9, 9, 9, 9};
  TEST_ASSERT_FALSE(espnow_mac_from_uid(uid, mac));
  TEST_ASSERT_EQUAL_HEX8(9, mac[0]);
  TEST_ASSERT_EQUAL(APP_EINVAL, espnow_start_check({}, uid, 1));
}

// ---- start ----

void test_start_checks_arguments() {
  radio_users_t idle = {};
  TEST_ASSERT_EQUAL(0, espnow_start_check(idle, SRC, 1));
  TEST_ASSERT_EQUAL(0, espnow_start_check(idle, SRC, 13));
  TEST_ASSERT_EQUAL(APP_EINVAL, espnow_start_check(idle, SRC, 0));
  TEST_ASSERT_EQUAL(APP_EINVAL, espnow_start_check(idle, SRC, 14));
  TEST_ASSERT_EQUAL(APP_EINVAL, espnow_start_check(idle, nullptr, 1));
}

// Refused while the Wi-Fi page, net_up's network or a Bluetooth keyboard holds the radio, or it already runs
void test_start_refused_while_busy() {
  radio_users_t u = {};
  u.webshare = true;
  TEST_ASSERT_EQUAL(APP_EBUSY, espnow_start_check(u, SRC, 1));
  TEST_ASSERT_EQUAL_STRING("the Wi-Fi page", radio_busy_reason(u));
  u = {};
  u.wifi_connected = true;
  TEST_ASSERT_EQUAL(APP_EBUSY, espnow_start_check(u, SRC, 1));
  TEST_ASSERT_EQUAL_STRING("Wi-Fi (net_up)", radio_busy_reason(u));
  u = {};
  u.ble = true;
  TEST_ASSERT_EQUAL(APP_EBUSY, espnow_start_check(u, SRC, 1));
  TEST_ASSERT_EQUAL_STRING("a Bluetooth keyboard", radio_busy_reason(u));
  u = {};
  u.espnow = true;
  TEST_ASSERT_EQUAL(APP_EBUSY, espnow_start_check(u, SRC, 1));
  TEST_ASSERT_EQUAL_STRING("", radio_busy_reason({}));
}

// While ESP-NOW runs the others are refused; without it they share as before
void test_others_refused_while_espnow_runs() {
  radio_users_t u = {};
  u.espnow = true;
  TEST_ASSERT_EQUAL(APP_EBUSY, radio_claim_check(u, RADIO_WANT_WEBSHARE));
  TEST_ASSERT_EQUAL(APP_EBUSY, radio_claim_check(u, RADIO_WANT_NET));
  TEST_ASSERT_EQUAL(APP_EBUSY, radio_claim_check(u, RADIO_WANT_BLE));
  radio_users_t shared = {true, true, true, false};
  TEST_ASSERT_EQUAL(0, radio_claim_check(shared, RADIO_WANT_WEBSHARE));
  TEST_ASSERT_EQUAL(0, radio_claim_check(shared, RADIO_WANT_NET));
  TEST_ASSERT_EQUAL(0, radio_claim_check(shared, RADIO_WANT_BLE));
}

// ---- queue ----

static app_espnow_pkt_t slots[4];

void test_ring_keeps_order_and_contents() {
  espnow_ring_t r;
  espnow_ring_init(&r, slots, 4);
  uint8_t d[APP_ESPNOW_MAX];
  for (int i = 0; i < APP_ESPNOW_MAX; i++) d[i] = (uint8_t)i;
  TEST_ASSERT_TRUE(espnow_ring_push(&r, SRC, -61, d, 3));
  TEST_ASSERT_TRUE(espnow_ring_push(&r, SRC, -200, d, APP_ESPNOW_MAX));
  app_espnow_pkt_t p;
  TEST_ASSERT_TRUE(espnow_ring_pop(&r, &p));
  TEST_ASSERT_EQUAL(3, p.len);
  TEST_ASSERT_EQUAL(-61, p.rssi);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, p.src, 6);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(d, p.data, 3);
  TEST_ASSERT_TRUE(espnow_ring_pop(&r, &p));
  TEST_ASSERT_EQUAL(APP_ESPNOW_MAX, p.len);
  TEST_ASSERT_EQUAL(-128, p.rssi); // clamped to int8_t
  TEST_ASSERT_EQUAL_HEX8_ARRAY(d, p.data, APP_ESPNOW_MAX);
  TEST_ASSERT_FALSE(espnow_ring_pop(&r, &p));
  TEST_ASSERT_EQUAL(2, r.received);
  TEST_ASSERT_EQUAL(0, r.dropped);
}

// Full: the new frame is dropped and counted, the queued ones stay; room again after a pop
void test_ring_full_drops_and_counts() {
  espnow_ring_t r;
  espnow_ring_init(&r, slots, 4);
  uint8_t d[1];
  for (int i = 0; i < 4; i++) {
    d[0] = (uint8_t)i;
    TEST_ASSERT_TRUE(espnow_ring_push(&r, SRC, -50, d, 1));
  }
  d[0] = 99;
  TEST_ASSERT_FALSE(espnow_ring_push(&r, SRC, -50, d, 1));
  TEST_ASSERT_FALSE(espnow_ring_push(&r, SRC, -50, d, 1));
  TEST_ASSERT_EQUAL(2, r.dropped);
  app_espnow_pkt_t p;
  TEST_ASSERT_TRUE(espnow_ring_pop(&r, &p));
  TEST_ASSERT_EQUAL(0, p.data[0]);
  d[0] = 4;
  TEST_ASSERT_TRUE(espnow_ring_push(&r, SRC, -50, d, 1));
  for (int i = 1; i <= 4; i++) {
    TEST_ASSERT_TRUE(espnow_ring_pop(&r, &p));
    TEST_ASSERT_EQUAL(i, p.data[0]);
  }
  TEST_ASSERT_FALSE(espnow_ring_pop(&r, &p));
}

// A frame over the ESP-NOW limit (or a negative length) never reaches a slot: dropped
void test_ring_refuses_oversized_frames() {
  espnow_ring_t r;
  espnow_ring_init(&r, slots, 4);
  static uint8_t big[APP_ESPNOW_MAX + 1];
  TEST_ASSERT_FALSE(espnow_ring_push(&r, SRC, -50, big, APP_ESPNOW_MAX + 1));
  TEST_ASSERT_FALSE(espnow_ring_push(&r, SRC, -50, big, -1));
  TEST_ASSERT_FALSE(espnow_ring_push(&r, SRC, -50, nullptr, 4));
  TEST_ASSERT_EQUAL(3, r.dropped);
  app_espnow_pkt_t p;
  TEST_ASSERT_FALSE(espnow_ring_pop(&r, &p));
}

// Survives the counters wrapping past 2^32 (a slot count that is no power of two is cut to one: 3 used to reorder)
void test_ring_index_wrap() {
  espnow_ring_t r;
  espnow_ring_init(&r, slots, 3);
  TEST_ASSERT_EQUAL(2, r.cap);
  espnow_ring_init(&r, slots, 4);
  r.head = r.tail = 0xfffffffdu;
  uint8_t d[1];
  for (int i = 0; i < 4; i++) {
    d[0] = (uint8_t)i;
    TEST_ASSERT_TRUE(espnow_ring_push(&r, SRC, -50, d, 1));
  }
  TEST_ASSERT_FALSE(espnow_ring_push(&r, SRC, -50, d, 1));
  app_espnow_pkt_t p;
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_TRUE(espnow_ring_pop(&r, &p));
    TEST_ASSERT_EQUAL(i, p.data[0]);
  }
}

// Drops go to the event log at most once per period, each counted once
void test_drops_logged_once_per_period() {
  espnow_ring_t r;
  espnow_ring_init(&r, slots, 1);
  uint8_t d[1] = {0};
  uint32_t last = 0;
  TEST_ASSERT_EQUAL(0, espnow_ring_drops_to_log(&r, 100, 10000, &last));
  espnow_ring_push(&r, SRC, -50, d, 1);
  espnow_ring_push(&r, SRC, -50, d, 1);
  espnow_ring_push(&r, SRC, -50, d, 1);
  TEST_ASSERT_EQUAL(2, espnow_ring_drops_to_log(&r, 0, 10000, &last)); // at millis 0 too
  espnow_ring_push(&r, SRC, -50, d, 1);
  TEST_ASSERT_EQUAL(0, espnow_ring_drops_to_log(&r, 5000, 10000, &last));
  TEST_ASSERT_EQUAL(1, espnow_ring_drops_to_log(&r, 10001, 10000, &last));
  TEST_ASSERT_EQUAL(0, espnow_ring_drops_to_log(&r, 30000, 10000, &last));
}

// ---- restore ----

// The app ends without espnow_stop (or crashes out of its loop): the runner's end puts the original MAC back, once
void test_end_restores_the_original_mac_once() {
  espnow_session_t s = {};
  const uint8_t efuse[6] = {0xdc, 0xda, 0x0c, 1, 2, 3};
  uint8_t mac[6] = {0};
  uint8_t proto = 0;
  TEST_ASSERT_FALSE(espnow_session_end(&s, mac, &proto)); // never started: nothing to put back
  espnow_session_begin(&s, efuse, 0x0f);
  TEST_ASSERT_TRUE(espnow_session_end(&s, mac, &proto));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(efuse, mac, 6);
  TEST_ASSERT_EQUAL_HEX8(0x0f, proto);
  TEST_ASSERT_FALSE(espnow_session_end(&s, mac, &proto)); // the app's own stop came first: the end does nothing
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mac_is_the_uid_with_the_multicast_bit_cleared);
  RUN_TEST(test_all_zero_mac_refused);
  RUN_TEST(test_start_checks_arguments);
  RUN_TEST(test_start_refused_while_busy);
  RUN_TEST(test_others_refused_while_espnow_runs);
  RUN_TEST(test_ring_keeps_order_and_contents);
  RUN_TEST(test_ring_full_drops_and_counts);
  RUN_TEST(test_ring_refuses_oversized_frames);
  RUN_TEST(test_ring_index_wrap);
  RUN_TEST(test_drops_logged_once_per_period);
  RUN_TEST(test_end_restores_the_original_mac_once);
  return UNITY_END();
}
