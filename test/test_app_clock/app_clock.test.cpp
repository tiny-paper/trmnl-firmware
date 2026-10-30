#include <app_clock.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

static const int64_t SUMMER = 1782043200; // 2026-06-21 12:00:00 UTC, a Sunday
static const int64_t WINTER = 1767268800; // 2026-01-01 12:00:00 UTC

void test_battery_percent_and_charging() {
  bool charging = false;
  TEST_ASSERT_EQUAL(57, app_battery_percent(57, ChargingStatus::CHARGING, &charging));
  TEST_ASSERT_TRUE(charging);
  TEST_ASSERT_EQUAL(100, app_battery_percent(104, ChargingStatus::NOT_CHARGING, &charging));
  TEST_ASSERT_FALSE(charging);
  TEST_ASSERT_EQUAL(0, app_battery_percent(0, ChargingStatus::UNKNOWN, nullptr));
}

void test_battery_unreadable_gauge_is_an_error() {
  bool charging = true;
  TEST_ASSERT_EQUAL(APP_EIO, app_battery_percent(-1, ChargingStatus::UNKNOWN, &charging));
  TEST_ASSERT_FALSE(charging); // unknown is not charging
}

void test_clock_never_set() {
  app_local_time_t t;
  TEST_ASSERT_EQUAL(APP_ENOCLOCK, app_local_time(0, "CET-1CEST,M3.5.0,M10.5.0/3", &t));
  TEST_ASSERT_EQUAL(APP_ENOCLOCK, app_local_time(APP_CLOCK_SET_AFTER - 1, nullptr, &t));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_local_time(SUMMER, nullptr, nullptr));
}

void test_no_zone_falls_back_to_utc_with_the_flag() {
  const char *none[] = {nullptr, "", "Europe/Brussels", "X1", "CET", "<A>1"};
  for (const char *tz : none) {
    app_local_time_t t;
    TEST_ASSERT_EQUAL(0, app_local_time(SUMMER, tz, &t));
    TEST_ASSERT_EQUAL_MESSAGE(0, t.zone_known, tz ? tz : "NULL");
    TEST_ASSERT_EQUAL(12, t.hour);
    TEST_ASSERT_EQUAL(0, t.utc_offset_s);
    TEST_ASSERT_EQUAL(0, t.dst);
  }
}

void test_zone_with_daylight_saving() {
  app_local_time_t t;
  TEST_ASSERT_EQUAL(0, app_local_time(SUMMER, "CET-1CEST,M3.5.0,M10.5.0/3", &t));
  TEST_ASSERT_EQUAL(1, t.zone_known);
  TEST_ASSERT_EQUAL(2026, t.year);
  TEST_ASSERT_EQUAL(6, t.month);
  TEST_ASSERT_EQUAL(21, t.day);
  TEST_ASSERT_EQUAL(14, t.hour);
  TEST_ASSERT_EQUAL(0, t.weekday);
  TEST_ASSERT_EQUAL(7200, t.utc_offset_s);
  TEST_ASSERT_EQUAL(1, t.dst);
  TEST_ASSERT_EQUAL(0, app_local_time(WINTER, "CET-1CEST,M3.5.0,M10.5.0/3", &t));
  TEST_ASSERT_EQUAL(13, t.hour);
  TEST_ASSERT_EQUAL(3600, t.utc_offset_s);
  TEST_ASSERT_EQUAL(0, t.dst);
}

void test_zone_west_of_utc_across_midnight() {
  app_local_time_t t;
  TEST_ASSERT_EQUAL(0, app_local_time(1767225600, "<-10>10", &t)); // 2026-01-01 00:00 UTC
  TEST_ASSERT_EQUAL(1, t.zone_known);
  TEST_ASSERT_EQUAL(2025, t.year);
  TEST_ASSERT_EQUAL(12, t.month);
  TEST_ASSERT_EQUAL(31, t.day);
  TEST_ASSERT_EQUAL(14, t.hour);
  TEST_ASSERT_EQUAL(-36000, t.utc_offset_s);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_battery_percent_and_charging);
  RUN_TEST(test_battery_unreadable_gauge_is_an_error);
  RUN_TEST(test_clock_never_set);
  RUN_TEST(test_no_zone_falls_back_to_utc_with_the_flag);
  RUN_TEST(test_zone_with_daylight_saving);
  RUN_TEST(test_zone_west_of_utc_across_midnight);
  return UNITY_END();
}
