#include <ble_hogp.h>
#include <ble_pair.h>
#include <string.h>
#include <unity.h>

static hogp_t h;

void setUp() {}
void tearDown() {}

// Report map: keyboard report 1 (modifiers, reserved, 6 keys)
static const uint8_t kbd_map[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0,
                                  0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
                                  0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
                                  0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
                                  0xC0};
static const uint8_t consumer_map[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x75, 0x10,
                                       0x95, 0x01, 0x81, 0x00, 0xC0};

// A HID service at 0x10..0x30: map 0x12, input report 0x15 (CCCD 0x16, ref 0x17), output report 0x19
// (ref 0x1a), protocol mode 0x1c, boot keyboard input 0x1e (CCCD 0x1f)
static void discover(bool with_boot, bool with_report) {
  hogp_service(&h, 0x180F, 0x01, 0x0F); // battery: not ours
  hogp_service(&h, HOGP_UUID_HID_SERVICE, 0x10, 0x30);
  hogp_act_t a = hogp_services_done(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_FIND_CHRS, a.what);
  TEST_ASSERT_EQUAL_HEX16(0x10, a.handle);
  TEST_ASSERT_EQUAL_HEX16(0x30, a.end);
  hogp_chr(&h, HOGP_UUID_REPORT_MAP, 0x12);
  if (with_report) hogp_chr(&h, HOGP_UUID_REPORT, 0x15);
  hogp_chr(&h, HOGP_UUID_REPORT, 0x19);
  hogp_chr(&h, HOGP_UUID_PROTOCOL, 0x1c);
  if (with_boot) hogp_chr(&h, HOGP_UUID_BOOT_KBD_IN, 0x1e);
  a = hogp_chrs_done(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_FIND_DSCS, a.what);
  if (with_report) {
    hogp_dsc(&h, 0x16, HOGP_UUID_CCCD);
    hogp_dsc(&h, 0x17, HOGP_UUID_REPORT_REF);
  }
  hogp_dsc(&h, 0x1a, HOGP_UUID_REPORT_REF);
  if (with_boot) hogp_dsc(&h, 0x1f, HOGP_UUID_CCCD);
  a = hogp_dscs_done(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_READ_LONG, a.what);
  TEST_ASSERT_EQUAL_HEX16(0x12, a.handle);
}

static hogp_act_t secure(uint32_t now = 0) {
  TEST_ASSERT_EQUAL(HOGP_DO_SECURE, hogp_start(&h, now).what);
  hogp_act_t a = hogp_encrypted(&h, 0, now + 100);
  TEST_ASSERT_EQUAL(HOGP_DO_FIND_SERVICE, a.what);
  return a;
}

void test_nothing_is_asked_before_the_link_is_encrypted() {
  TEST_ASSERT_EQUAL(HOGP_DO_SECURE, hogp_start(&h, 0).what);
  // results that come early (a stale GATT callback) start nothing
  hogp_service(&h, HOGP_UUID_HID_SERVICE, 0x10, 0x30);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_services_done(&h, 0).what);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_chrs_done(&h, 0).what);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_dscs_done(&h, 0).what);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_read_done(&h, 0).what);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_written(&h, 0).what);
  hogp_passkey(&h, 10);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_services_done(&h, 0).what);
  TEST_ASSERT_EQUAL(HOGP_PASSKEY, h.state);
  TEST_ASSERT_EQUAL(0, h.svc_start); // the early service was not taken
  TEST_ASSERT_EQUAL(HOGP_DO_FIND_SERVICE, hogp_encrypted(&h, 0, 20).what);
}

void test_report_protocol_keyboard() {
  secure();
  discover(true, true);
  hogp_read_data(&h, kbd_map, 20); // a long read in two parts
  hogp_read_data(&h, kbd_map + 20, sizeof(kbd_map) - 20);
  hogp_act_t a = hogp_read_done(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_READ, a.what);
  TEST_ASSERT_EQUAL_HEX16(0x17, a.handle);
  const uint8_t ref_in[2] = {1, 1}, ref_out[2] = {1, 2};
  hogp_read_data(&h, ref_in, 2);
  a = hogp_read_done(&h, 0);
  TEST_ASSERT_EQUAL_HEX16(0x1a, a.handle);
  hogp_read_data(&h, ref_out, 2);
  a = hogp_read_done(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_WRITE_NO_RSP, a.what); // protocol mode: report
  TEST_ASSERT_EQUAL_HEX16(0x1c, a.handle);
  TEST_ASSERT_EQUAL(1, a.value[0]);
  a = hogp_written(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_WRITE, a.what); // notifications on the input report's CCCD
  TEST_ASSERT_EQUAL_HEX16(0x16, a.handle);
  TEST_ASSERT_EQUAL(2, a.len);
  TEST_ASSERT_EQUAL(1, a.value[0]);
  TEST_ASSERT_EQUAL(HOGP_DO_READY, hogp_written(&h, 0).what);
  const uint8_t r[8] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
  uint8_t boot[8];
  TEST_ASSERT_TRUE(hogp_report(&h, 0x15, r, sizeof(r), boot));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(r, boot, 8);
  TEST_ASSERT_FALSE(hogp_report(&h, 0x1e, r, sizeof(r), boot)); // not subscribed
}

void test_boot_protocol_when_the_map_has_no_keyboard() {
  secure();
  discover(true, false);
  hogp_read_data(&h, consumer_map, sizeof(consumer_map));
  hogp_act_t a = hogp_read_done(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_READ, a.what); // the output report's reference
  hogp_read_done(&h, 0x10A); // refused: not fatal
  TEST_ASSERT_EQUAL(HOGP_SET_PROTOCOL, h.state);
  TEST_ASSERT_TRUE(h.boot);
  a = hogp_written(&h, 0x10E); // protocol write failed: subscribe anyway
  TEST_ASSERT_EQUAL_HEX16(0x1f, a.handle);
  TEST_ASSERT_EQUAL(HOGP_DO_READY, hogp_written(&h, 0).what);
  const uint8_t r[8] = {0, 0, 0x05, 0, 0, 0, 0, 0};
  uint8_t boot[8];
  TEST_ASSERT_TRUE(hogp_report(&h, 0x1e, r, 8, boot));
  TEST_ASSERT_FALSE(hogp_report(&h, 0x1e, r, 7, boot));
}

void test_not_a_keyboard() {
  secure();
  TEST_ASSERT_EQUAL(BLE_FAIL_NOT_HID, hogp_services_done(&h, 0).why); // no HID service
  secure();
  discover(false, false); // only an output report, no boot input
  hogp_read_data(&h, consumer_map, sizeof(consumer_map));
  hogp_read_done(&h, 0);
  hogp_act_t a = hogp_read_done(&h, 0);
  TEST_ASSERT_EQUAL(HOGP_DO_FAIL, a.what);
  TEST_ASSERT_EQUAL(BLE_FAIL_NOT_HID, a.why);
}

void test_encryption_failures() {
  hogp_start(&h, 0);
  hogp_act_t a = hogp_encrypted(&h, HOGP_HS_HCI_BASE + 0x06, 1); // the keyboard has no key for us
  TEST_ASSERT_EQUAL(HOGP_DO_FAIL, a.what);
  TEST_ASSERT_EQUAL(BLE_FAIL_KEY_MISSING, a.why);
  hogp_start(&h, 0);
  hogp_passkey(&h, 1);
  a = hogp_encrypted(&h, HOGP_HS_SM_PEER_BASE + 0x04, 2); // confirm value failed: a wrong passkey
  TEST_ASSERT_EQUAL(BLE_FAIL_AUTH, a.why);
  hogp_start(&h, 0);
  hogp_passkey(&h, 1);
  TEST_ASSERT_EQUAL(BLE_FAIL_PASSKEY_TIMEOUT, hogp_encrypted(&h, HOGP_HS_ETIMEOUT, 2).why); // SMP's 30 s
  hogp_start(&h, 0);
  TEST_ASSERT_EQUAL(BLE_FAIL_TIMEOUT, hogp_encrypted(&h, 7 /* ENOTCONN */, 2).why);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_encrypted(&h, 0, 3).what); // failed stays failed
}

void test_passkey_and_step_timeouts() {
  hogp_start(&h, 1000);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_tick(&h, 1000 + HOGP_SECURE_MS - 1).what);
  hogp_passkey(&h, 5000); // shown: more time to type it
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_tick(&h, 1000 + HOGP_SECURE_MS).what);
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_tick(&h, 5000 + HOGP_PASSKEY_MS - 1).what);
  hogp_act_t a = hogp_tick(&h, 5000 + HOGP_PASSKEY_MS);
  TEST_ASSERT_EQUAL(HOGP_DO_FAIL, a.what);
  TEST_ASSERT_EQUAL(BLE_FAIL_PASSKEY_TIMEOUT, a.why);
  hogp_start(&h, 0); // just-works or bonded: nothing to type
  TEST_ASSERT_EQUAL(BLE_FAIL_TIMEOUT, hogp_tick(&h, HOGP_SECURE_MS).why);
  secure(0); // encrypted, then the keyboard stops answering mid-discovery
  TEST_ASSERT_EQUAL(BLE_FAIL_TIMEOUT, hogp_tick(&h, 100 + HOGP_DISCOVER_MS).why);
  hogp_start(&h, 0xFFFFF000u); // clock wrap
  TEST_ASSERT_EQUAL(HOGP_DO_NONE, hogp_tick(&h, 0x10).what);
}

void test_subscribe_refused_for_security() {
  secure();
  discover(true, true);
  hogp_read_data(&h, kbd_map, sizeof(kbd_map));
  hogp_read_done(&h, 0);
  const uint8_t ref_in[2] = {1, 1};
  hogp_read_data(&h, ref_in, 2);
  hogp_read_done(&h, 0);
  hogp_read_done(&h, 0);
  hogp_written(&h, 0);
  hogp_act_t a = hogp_written(&h, HOGP_HS_ATT_BASE + 0x05); // insufficient authentication
  TEST_ASSERT_EQUAL(HOGP_DO_FAIL, a.what);
  TEST_ASSERT_EQUAL(BLE_FAIL_AUTH, a.why);
}

// With the pairing rules: what the keyboard's side does to the X's bond
static ble_pair_peer_t pop(void) {
  ble_pair_peer_t k = {};
  k.addr[6] = 7;
  strcpy(k.name, "POP Icon Keys");
  return k;
}

void test_bond_deleted_on_the_keyboard_side() {
  ble_pair_t p;
  ble_pair_peer_t k = pop();
  TEST_ASSERT_BITS_HIGH(BLE_ACT_CONNECT, ble_pair_enable(&p, &k));
  hogp_start(&h, 0);
  hogp_act_t a = hogp_encrypted(&h, HOGP_HS_HCI_BASE + 0x06, 50);
  uint32_t act = ble_pair_failed(&p, a.why, 50);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_DELETE_BOND | BLE_ACT_SCAN | BLE_ACT_SCREEN, act);
  TEST_ASSERT_FALSE(p.bonded);
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
}

void test_pairing_failure_and_passkey_timeout_back_to_the_list() {
  ble_pair_t p;
  ble_pair_peer_t k = pop();
  ble_pair_enable(&p, nullptr);
  ble_pair_found(&p, &k);
  ble_pair_touch(&p, 1);
  hogp_start(&h, 0);
  hogp_passkey(&h, 10);
  ble_pair_passkey(&p, 123456);
  uint32_t act = ble_pair_failed(&p, hogp_tick(&h, 10 + HOGP_PASSKEY_MS).why, 10 + HOGP_PASSKEY_MS);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_SCAN | BLE_ACT_SCREEN, act);
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
  TEST_ASSERT_EQUAL_STRING("The passkey was not typed in time: pick the keyboard again", p.message);
  // bonded, and the keyboard asks for a passkey that is never typed: its bond is gone, ours too
  ble_pair_enable(&p, &k);
  hogp_start(&h, 0);
  hogp_passkey(&h, 1);
  ble_pair_passkey(&p, 1);
  act = ble_pair_failed(&p, hogp_encrypted(&h, HOGP_HS_ETIMEOUT, 30000).why, 30000);
  TEST_ASSERT_BITS_HIGH(BLE_ACT_DELETE_BOND, act);
  TEST_ASSERT_EQUAL(BLE_PAIR_LIST, p.state);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_asked_before_the_link_is_encrypted);
  RUN_TEST(test_report_protocol_keyboard);
  RUN_TEST(test_boot_protocol_when_the_map_has_no_keyboard);
  RUN_TEST(test_not_a_keyboard);
  RUN_TEST(test_encryption_failures);
  RUN_TEST(test_passkey_and_step_timeouts);
  RUN_TEST(test_subscribe_refused_for_security);
  RUN_TEST(test_bond_deleted_on_the_keyboard_side);
  RUN_TEST(test_pairing_failure_and_passkey_timeout_back_to_the_list);
  return UNITY_END();
}
