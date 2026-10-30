#include <ble_conn_params.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// What Logitech keyboards ask right after connecting: 7.5 ms, a long latency, 2 s
void test_a_keyboards_usual_request_is_accepted() {
  ble_conn_params_t r = {6, 6, 30, 200};
  TEST_ASSERT_EQUAL_HEX8(BLE_CONN_ANSWER_ACCEPT, ble_conn_param_answer(&r));
  ble_conn_params_t slow = {24, 40, 4, 600};
  TEST_ASSERT_EQUAL_HEX8(BLE_CONN_ANSWER_ACCEPT, ble_conn_param_answer(&slow));
  ble_conn_params_t edge = {6, 3200, 0, 3200}; // the spec's ends
  TEST_ASSERT_EQUAL_HEX8(BLE_CONN_ANSWER_ACCEPT, ble_conn_param_answer(&edge));
}

void test_a_request_outside_the_spec_is_refused_not_left_open() {
  ble_conn_params_t bad[] = {
      {5, 6, 0, 200}, // interval below 7.5 ms
      {6, 3201, 0, 3200}, // above 4 s
      {12, 6, 0, 200}, // min above max
      {6, 6, 500, 3200}, // latency above 499
      {6, 6, 0, 9}, // timeout below 100 ms
      {6, 6, 0, 3201}, // above 32 s
      {80, 80, 10, 100}, // the link would time out within the latency
  };
  for (const auto &r : bad) TEST_ASSERT_EQUAL_HEX8(BLE_CONN_ANSWER_REJECT, ble_conn_param_answer(&r));
  TEST_ASSERT_EQUAL_HEX8(BLE_CONN_ANSWER_REJECT, ble_conn_param_answer(nullptr));
}

// Every request gets one of the two answers: a sweep over the field ranges
void test_every_request_is_answered() {
  for (uint32_t i = 0; i < 0x10000; i += 97) {
    for (uint32_t t = 0; t < 0x10000; t += 251) {
      ble_conn_params_t r = {(uint16_t)i, (uint16_t)(i + 6), (uint16_t)(t % 600), (uint16_t)t};
      uint8_t a = ble_conn_param_answer(&r);
      TEST_ASSERT_TRUE(a == BLE_CONN_ANSWER_ACCEPT || a == BLE_CONN_ANSWER_REJECT);
    }
  }
}

void test_setup_is_fast_and_valid() {
  ble_conn_params_t s = ble_conn_setup_params();
  TEST_ASSERT_GREATER_OR_EQUAL(6, s.itvl_min); // 7.5 ms
  TEST_ASSERT_LESS_OR_EQUAL(12, s.itvl_max); // 15 ms
  TEST_ASSERT_EQUAL_HEX8(BLE_CONN_ANSWER_ACCEPT, ble_conn_param_answer(&s));
}

void test_log_line() {
  char buf[160];
  ble_conn_params_t r = {6, 9, 30, 200};
  ble_conn_param_line(buf, sizeof(buf), "L2CAP", &r, BLE_CONN_ANSWER_ACCEPT);
  TEST_ASSERT_EQUAL_STRING("BLE kbd: L2CAP params asked 6-9 (x1.25 ms), latency 30, timeout 2000 ms: accepted", buf);
  char small[8];
  TEST_ASSERT_EQUAL(7, ble_conn_param_line(small, sizeof(small), "LL", &r, BLE_CONN_ANSWER_REJECT));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_keyboards_usual_request_is_accepted);
  RUN_TEST(test_a_request_outside_the_spec_is_refused_not_left_open);
  RUN_TEST(test_every_request_is_answered);
  RUN_TEST(test_setup_is_fast_and_valid);
  RUN_TEST(test_log_line);
  return UNITY_END();
}
