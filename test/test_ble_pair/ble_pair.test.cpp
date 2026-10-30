#include <ble_pair.h>
#include <string.h>
#include <unity.h>

static ble_pair_t p;

void setUp() {}
void tearDown() {}

static ble_pair_peer_t peer(uint8_t id, const char *name, int8_t rssi = -50) {
  ble_pair_peer_t x = {};
  x.addr[0] = 1;
  x.addr[6] = id;
  strncpy(x.name, name, sizeof(x.name) - 1);
  x.rssi = rssi;
  return x;
}

static void pair_with(uint8_t id) {
  ble_pair_enable(&p, nullptr);
  ble_pair_peer_t k = peer(id, "POP Icon Keys");
  ble_pair_found(&p, &k);
  ble_pair_touch(&p, 1);
  ble_pair_connected(&p);
}

void test_no_bond_shows_the_list_and_scans() {
  uint32_t a = ble_pair_enable(&p, nullptr);
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
  TEST_ASSERT_BITS(BLE_ACT_SCAN | BLE_ACT_SCREEN, BLE_ACT_SCAN | BLE_ACT_SCREEN, a);
  TEST_ASSERT_BITS_LOW(BLE_ACT_CONNECT, a);
}

void test_no_keyboard_found_a_tap_does_nothing() {
  ble_pair_enable(&p, nullptr);
  uint32_t a = ble_pair_touch(&p, 1);
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
  TEST_ASSERT_BITS_LOW(BLE_ACT_CONNECT, a);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_SCAN, ble_pair_tick(&p, 100000)); // keeps scanning
}

void test_found_keyboards_are_listed_once_and_bounded() {
  ble_pair_enable(&p, nullptr);
  ble_pair_peer_t a = peer(1, ""), a_named = peer(1, "POP Icon Keys", -40);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_SCREEN, ble_pair_found(&p, &a));
  TEST_ASSERT_BITS_HIGH(BLE_ACT_SCREEN, ble_pair_found(&p, &a_named)); // the scan response's name
  TEST_ASSERT_BITS_LOW(BLE_ACT_SCREEN, ble_pair_found(&p, &a_named));
  TEST_ASSERT_EQUAL(1, p.found_count);
  TEST_ASSERT_EQUAL_STRING("POP Icon Keys", p.found[0].name);
  for (uint8_t i = 2; i < 20; i++) {
    ble_pair_peer_t x = peer(i, "k");
    ble_pair_found(&p, &x);
  }
  TEST_ASSERT_EQUAL(BLE_PAIR_MAX_FOUND, p.found_count);
}

void test_touch_moves_and_picks() {
  ble_pair_enable(&p, nullptr);
  ble_pair_peer_t a = peer(1, "a"), b = peer(2, "b");
  ble_pair_found(&p, &a);
  ble_pair_found(&p, &b);
  ble_pair_touch(&p, 0); // left from the first: wraps to the last
  TEST_ASSERT_EQUAL(1, p.selected);
  ble_pair_touch(&p, 2);
  TEST_ASSERT_EQUAL(0, p.selected);
  ble_pair_touch(&p, 2);
  uint32_t act = ble_pair_touch(&p, 1);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_CONNECT, act);
  TEST_ASSERT_BITS_LOW(BLE_ACT_SCAN, act); // scanning stops while connecting
  TEST_ASSERT_EQUAL_STRING("b", p.target.name);
}

void test_passkey_then_connected_saves_the_bond() {
  ble_pair_enable(&p, nullptr);
  ble_pair_peer_t k = peer(1, "POP Icon Keys");
  ble_pair_found(&p, &k);
  ble_pair_touch(&p, 1);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_SCREEN, ble_pair_passkey(&p, 123456));
  TEST_ASSERT_EQUAL(BLE_PAIR_PASSKEY, p.state);
  TEST_ASSERT_EQUAL_UINT32(123456, p.passkey);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_SAVE_BOND, ble_pair_connected(&p));
  TEST_ASSERT_EQUAL(BLE_PAIR_CONNECTED, p.state);
}

void test_wrong_passkey_or_refused_pairing_back_to_the_list() {
  ble_pair_enable(&p, nullptr);
  ble_pair_peer_t k = peer(1, "POP Icon Keys");
  ble_pair_found(&p, &k);
  ble_pair_touch(&p, 1);
  ble_pair_passkey(&p, 1);
  uint32_t a = ble_pair_failed(&p, BLE_FAIL_AUTH, 0);
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_SCAN | BLE_ACT_DELETE_BOND, a);
  TEST_ASSERT_NOT_NULL(p.message);
  TEST_ASSERT_FALSE(p.bonded);
}

void test_a_bond_connects_without_the_list() {
  ble_pair_peer_t k = peer(1, "POP Icon Keys");
  uint32_t a = ble_pair_enable(&p, &k);
  TEST_ASSERT_EQUAL(BLE_PAIR_CONNECTING, p.state);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_CONNECT, a);
  TEST_ASSERT_BITS_LOW(BLE_ACT_SCAN | BLE_ACT_SCREEN, a);
  TEST_ASSERT_BITS_LOW(BLE_ACT_SAVE_BOND, ble_pair_connected(&p)); // already kept
}

void test_bonded_keyboard_asleep_is_tried_again() {
  ble_pair_peer_t k = peer(1, "POP Icon Keys");
  ble_pair_enable(&p, &k);
  TEST_ASSERT_EQUAL(0, ble_pair_failed(&p, BLE_FAIL_TIMEOUT, 1000));
  TEST_ASSERT_EQUAL(BLE_PAIR_WAIT, p.state);
  TEST_ASSERT_BITS_LOW(BLE_ACT_CONNECT, ble_pair_tick(&p, 1000 + BLE_PAIR_RETRY_MS - 1));
  TEST_ASSERT_BITS_HIGH(BLE_ACT_CONNECT, ble_pair_tick(&p, 1000 + BLE_PAIR_RETRY_MS));
  TEST_ASSERT_TRUE(p.bonded);
}

void test_disconnect_mid_typing_releases_keys_and_reconnects() {
  pair_with(1);
  uint32_t a = ble_pair_disconnected(&p, 5000);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_RELEASE | BLE_ACT_CONNECT, a);
  TEST_ASSERT_EQUAL(BLE_PAIR_CONNECTING, p.state);
  TEST_ASSERT_TRUE(p.bonded);
  ble_pair_connected(&p);
  TEST_ASSERT_EQUAL(BLE_PAIR_CONNECTED, p.state);
}

void test_bond_lost_on_the_keyboard_deletes_ours_and_lists_again() {
  pair_with(1);
  ble_pair_disconnected(&p, 0);
  uint32_t a = ble_pair_failed(&p, BLE_FAIL_KEY_MISSING, 10);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_DELETE_BOND | BLE_ACT_SCAN | BLE_ACT_SCREEN, a);
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
  TEST_ASSERT_FALSE(p.bonded);
  TEST_ASSERT_EQUAL(0, p.found_count);
}

void test_disable_while_connected_releases_and_drops_the_link() {
  pair_with(1);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_RELEASE | BLE_ACT_DISCONNECT, ble_pair_disable(&p));
  TEST_ASSERT_EQUAL(BLE_PAIR_OFF, p.state);
  TEST_ASSERT_EQUAL(0, ble_pair_tick(&p, 100000)); // nothing more
  ble_pair_peer_t k = peer(2, "x");
  TEST_ASSERT_BITS_LOW(BLE_ACT_SCREEN, ble_pair_found(&p, &k));
}

void test_events_out_of_order_are_ignored() {
  ble_pair_enable(&p, nullptr);
  TEST_ASSERT_EQUAL(0, ble_pair_connected(&p)); // nothing was picked
  TEST_ASSERT_EQUAL(0, ble_pair_passkey(&p, 5));
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
}

void test_passkey_screen_says_press_enter() {
  TEST_ASSERT_NOT_NULL(strstr(BLE_PASSKEY_TEXT, "6 digits"));
  TEST_ASSERT_NOT_NULL(strstr(BLE_PASSKEY_TEXT, "then press Enter"));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_passkey_screen_says_press_enter);
  RUN_TEST(test_no_bond_shows_the_list_and_scans);
  RUN_TEST(test_no_keyboard_found_a_tap_does_nothing);
  RUN_TEST(test_found_keyboards_are_listed_once_and_bounded);
  RUN_TEST(test_touch_moves_and_picks);
  RUN_TEST(test_passkey_then_connected_saves_the_bond);
  RUN_TEST(test_wrong_passkey_or_refused_pairing_back_to_the_list);
  RUN_TEST(test_a_bond_connects_without_the_list);
  RUN_TEST(test_bonded_keyboard_asleep_is_tried_again);
  RUN_TEST(test_disconnect_mid_typing_releases_keys_and_reconnects);
  RUN_TEST(test_bond_lost_on_the_keyboard_deletes_ours_and_lists_again);
  RUN_TEST(test_disable_while_connected_releases_and_drops_the_link);
  RUN_TEST(test_events_out_of_order_are_ignored);
  return UNITY_END();
}
