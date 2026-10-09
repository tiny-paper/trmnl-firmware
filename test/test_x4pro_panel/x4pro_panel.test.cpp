#include <unity.h>
#include <x4pro_panel.h>

static const uint8_t REV_UC8179[5] = {0xff, 0xff, 0x12, 0x00, 0x01}; // any LUT version not in the UC8279 list
static const uint8_t REV_UC8279[5] = {0xff, 0xff, 0x68, 0x00, 0x01};
static const uint8_t REV_PULLUP[5] = {0xff, 0xff, 0xff, 0xff, 0xff}; // released MOSI line reads the pull-up
static const uint8_t REV_LOW[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

static int classify(const uint8_t *r1, uint8_t f1, const uint8_t *r2, uint8_t f2, int factory, bool *conclusive) {
  return x4pro_classify_controller(r1, f1, r2, f2, factory, conclusive);
}

void test_uc8179_when_both_passes_agree(void) {
  bool conclusive = false;
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8179,
                    classify(REV_UC8179, 0x13, REV_UC8179, 0x13, X4PRO_CTRL_UNKNOWN, &conclusive));
  TEST_ASSERT_TRUE(conclusive);
}

void test_uc8279_by_lut_version(void) {
  const uint8_t versions[] = {0x02, 0x03, 0x67, 0x68, 0x69};
  for (unsigned i = 0; i < sizeof(versions); i++) {
    uint8_t rev[5] = {0xff, 0xff, versions[i], 0x00, 0x01};
    bool conclusive = false;
    TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8279, classify(rev, 0x13, rev, 0x13, X4PRO_CTRL_UNKNOWN, &conclusive));
    TEST_ASSERT_TRUE(conclusive);
  }
}

void test_probe_wins_over_factory_setting(void) {
  bool conclusive = false;
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8279,
                    classify(REV_UC8279, 0x13, REV_UC8279, 0x13, X4PRO_CTRL_SSD1677, &conclusive));
  TEST_ASSERT_TRUE(conclusive);
}

void test_ssd1677_when_nothing_answers(void) {
  bool conclusive = false;
  // pull-up only, and a line stuck low
  TEST_ASSERT_EQUAL(X4PRO_CTRL_SSD1677, classify(REV_PULLUP, 0xff, REV_PULLUP, 0xff, X4PRO_CTRL_UNKNOWN, &conclusive));
  TEST_ASSERT_TRUE(conclusive);
  TEST_ASSERT_EQUAL(X4PRO_CTRL_SSD1677, classify(REV_LOW, 0x00, REV_LOW, 0x00, X4PRO_CTRL_UNKNOWN, &conclusive));
  TEST_ASSERT_TRUE(conclusive);
  // silence is not overruled by the factory setting
  TEST_ASSERT_EQUAL(X4PRO_CTRL_SSD1677, classify(REV_PULLUP, 0xff, REV_PULLUP, 0xff, X4PRO_CTRL_UC8179, &conclusive));
  TEST_ASSERT_TRUE(conclusive);
}

void test_busy_status_is_not_an_answer(void) {
  // FLG with BUSY_N low (bit 0 clear) is not accepted as an UltraChip answer
  TEST_ASSERT_FALSE(x4pro_uc_answer(REV_UC8179, 0x12));
  TEST_ASSERT_TRUE(x4pro_uc_answer(REV_UC8179, 0x13));
  TEST_ASSERT_FALSE(x4pro_uc_answer(REV_PULLUP, 0x13));
}

void test_disagreeing_passes_fall_back_to_factory(void) {
  bool conclusive = true;
  // the two passes read different revisions
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8279, classify(REV_UC8179, 0x13, REV_UC8279, 0x13, X4PRO_CTRL_UC8279, &conclusive));
  TEST_ASSERT_FALSE(conclusive);
  // only one pass answered
  conclusive = true;
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8179, classify(REV_UC8179, 0x13, REV_PULLUP, 0xff, X4PRO_CTRL_UC8179, &conclusive));
  TEST_ASSERT_FALSE(conclusive);
  // a driven status with a blank revision
  conclusive = true;
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8179, classify(REV_PULLUP, 0x13, REV_PULLUP, 0x13, X4PRO_CTRL_UC8179, &conclusive));
  TEST_ASSERT_FALSE(conclusive);
}

void test_not_conclusive_without_factory_is_ssd1677(void) {
  bool conclusive = true;
  TEST_ASSERT_EQUAL(X4PRO_CTRL_SSD1677, classify(REV_UC8179, 0x13, REV_UC8279, 0x13, X4PRO_CTRL_UNKNOWN, &conclusive));
  TEST_ASSERT_FALSE(conclusive);
}

void test_factory_screen_type(void) {
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UNKNOWN, x4pro_factory_controller(-1));
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8179, x4pro_factory_controller(1));
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8179, x4pro_factory_controller(0x0b));
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8279, x4pro_factory_controller(2));
  TEST_ASSERT_EQUAL(X4PRO_CTRL_UC8279, x4pro_factory_controller(0x0c));
  TEST_ASSERT_EQUAL(X4PRO_CTRL_SSD1677, x4pro_factory_controller(0));
  TEST_ASSERT_EQUAL(X4PRO_CTRL_SSD1677, x4pro_factory_controller(0xff));
}

void test_gauge_millivolts(void) {
  // 4000mV = 12800 counts of 312.5uV = 0x3200
  TEST_ASSERT_EQUAL(4000, x4pro_gauge_millivolts(0x32, 0x00));
  // the top 2 bits of the high register are not part of the value
  TEST_ASSERT_EQUAL(4000, x4pro_gauge_millivolts(0xf2, 0x00));
  TEST_ASSERT_EQUAL(3700, x4pro_gauge_millivolts(0x2e, 0x40));
}

void test_gauge_rejects_impossible_values(void) {
  TEST_ASSERT_EQUAL(-1, x4pro_gauge_millivolts(0x00, 0x00)); // gauge asleep
  TEST_ASSERT_EQUAL(-1, x4pro_gauge_millivolts(0xff, 0xff)); // no device: bus reads ones (5119mV)
  TEST_ASSERT_EQUAL(-1, x4pro_gauge_millivolts(0x1f, 0x00)); // 2480mV
}

void setUp(void) {}

void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_uc8179_when_both_passes_agree);
  RUN_TEST(test_uc8279_by_lut_version);
  RUN_TEST(test_probe_wins_over_factory_setting);
  RUN_TEST(test_ssd1677_when_nothing_answers);
  RUN_TEST(test_busy_status_is_not_an_answer);
  RUN_TEST(test_disagreeing_passes_fall_back_to_factory);
  RUN_TEST(test_not_conclusive_without_factory_is_ssd1677);
  RUN_TEST(test_factory_screen_type);
  RUN_TEST(test_gauge_millivolts);
  RUN_TEST(test_gauge_rejects_impossible_values);
  return UNITY_END();
}
