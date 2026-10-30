#include <usb_port.h>
#include <unity.h>

static usb_port_t p;
static uint32_t now;

void setUp() {
  usb_port_init(&p);
  now = 0;
}
void tearDown() {}

// Polls every USB_PORT_POLL_MS for `ms` with these inputs; returns the mode at the end
static usb_port_mode_t run(uint32_t ms, bool wanted, usb_power_t power, bool keyboard) {
  usb_port_input_t in = {wanted, power, keyboard};
  usb_port_mode_t m = usb_port_step(&p, now, &in);
  for (uint32_t t = 0; t < ms; t += USB_PORT_POLL_MS) {
    now += USB_PORT_POLL_MS;
    m = usb_port_step(&p, now, &in);
  }
  return m;
}

// The first poll in host mode at or after `limit` ms, -1 if none
static int host_after(uint32_t limit, bool wanted, usb_power_t power, bool keyboard) {
  for (uint32_t t = 0; t <= limit; t += USB_PORT_POLL_MS) {
    usb_port_input_t in = {wanted, power, keyboard};
    if (usb_port_step(&p, now, &in) == USB_PORT_HOST) return (int)t;
    now += USB_PORT_POLL_MS;
  }
  return -1;
}

void test_starts_and_stays_in_device_mode_without_an_app() {
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, p.mode);
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, run(60000, false, USB_POWER_NO, false));
  TEST_ASSERT_EQUAL(USB_WHY_NOT_WANTED, p.why);
}

void test_never_host_while_powered_or_unreadable() {
  TEST_ASSERT_EQUAL(-1, host_after(60000, true, USB_POWER_YES, false));
  TEST_ASSERT_EQUAL(USB_WHY_INPUT_POWER, p.why);
  TEST_ASSERT_EQUAL(-1, host_after(60000, true, USB_POWER_UNKNOWN, false));
  TEST_ASSERT_EQUAL(USB_WHY_POWER_UNKNOWN, p.why);
}

void test_host_only_after_a_second_without_input_power() {
  int t = host_after(5000, true, USB_POWER_NO, false);
  TEST_ASSERT_EQUAL(USB_PORT_CLEAR_MS, t);
  TEST_ASSERT_EQUAL(USB_WHY_PROBING, p.why);
}

void test_a_power_blip_restarts_the_second() {
  run(900, true, USB_POWER_NO, false);
  run(0, true, USB_POWER_YES, false);
  now += USB_PORT_POLL_MS;
  TEST_ASSERT_EQUAL(USB_PORT_CLEAR_MS, host_after(5000, true, USB_POWER_NO, false));
}

void test_keyboard_keeps_host_mode() {
  run(1000, true, USB_POWER_NO, false);
  TEST_ASSERT_EQUAL(USB_PORT_HOST, p.mode);
  TEST_ASSERT_EQUAL(USB_PORT_HOST, run(60000, true, USB_POWER_NO, true));
  TEST_ASSERT_EQUAL(USB_WHY_KEYBOARD, p.why);
}

void test_no_keyboard_probe_ends_and_retries_later() {
  run(1000, true, USB_POWER_NO, false);
  TEST_ASSERT_EQUAL(USB_PORT_HOST, p.mode);
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, run(USB_PORT_PROBE_MS, true, USB_POWER_NO, false));
  TEST_ASSERT_EQUAL(USB_WHY_NO_KEYBOARD, p.why);
  // device mode for the retry time, then the next probe
  TEST_ASSERT_EQUAL(USB_PORT_RETRY_MS, host_after(USB_PORT_RETRY_MS * 2, true, USB_POWER_NO, false));
}

void test_keyboard_removed_mid_typing_gives_device_mode_at_once() {
  run(1000, true, USB_POWER_NO, true);
  run(200, true, USB_POWER_NO, true); // well inside the probe time: only the removal can end host mode
  TEST_ASSERT_EQUAL(USB_PORT_HOST, p.mode);
  now += USB_PORT_POLL_MS;
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, run(0, true, USB_POWER_NO, false)); // the first poll without it
  TEST_ASSERT_EQUAL(USB_WHY_NO_KEYBOARD, p.why);
}

void test_charger_plugged_in_while_host_gives_device_mode_at_once() {
  run(1000, true, USB_POWER_NO, true);
  TEST_ASSERT_EQUAL(USB_PORT_HOST, p.mode);
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, run(0, true, USB_POWER_YES, true));
  TEST_ASSERT_EQUAL(USB_WHY_INPUT_POWER, p.why);
  // and stays there while powered, even with the keyboard still reported
  TEST_ASSERT_EQUAL(-1, host_after(30000, true, USB_POWER_YES, true));
}

void test_charger_unreadable_while_host_gives_device_mode_at_once() {
  run(1000, true, USB_POWER_NO, true);
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, run(0, true, USB_POWER_UNKNOWN, true));
}

void test_app_exit_while_host_gives_device_mode_at_once() {
  run(1000, true, USB_POWER_NO, true);
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, run(0, false, USB_POWER_NO, true));
  TEST_ASSERT_EQUAL(USB_WHY_NOT_WANTED, p.why);
}

void test_restart_starts_in_device_mode() {
  run(1000, true, USB_POWER_NO, true);
  usb_port_init(&p); // what a restart does
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, p.mode);
  TEST_ASSERT_EQUAL(USB_PORT_CLEAR_MS, host_after(5000, true, USB_POWER_NO, false));
}

void test_clock_wrap() {
  now = 0xFFFFFF00u;
  TEST_ASSERT_EQUAL(USB_PORT_CLEAR_MS, host_after(5000, true, USB_POWER_NO, false));
  TEST_ASSERT_EQUAL(USB_PORT_DEVICE, run(USB_PORT_PROBE_MS, true, USB_POWER_NO, false));
  TEST_ASSERT_EQUAL(USB_PORT_RETRY_MS, host_after(USB_PORT_RETRY_MS * 2, true, USB_POWER_NO, false));
}

// ---- the PHY's roles: the keyboard's host mode and the USB drive never take it from each other ----

void test_roles_start_serial_and_one_role_at_a_time() {
  usb_roles_t r;
  usb_roles_init(&r);
  TEST_ASSERT_EQUAL(USB_ROLE_SERIAL, r.role);
  TEST_ASSERT_TRUE(usb_role_take(&r, USB_ROLE_DRIVE));
  TEST_ASSERT_FALSE(usb_role_take(&r, USB_ROLE_HOST)); // the drive shows: no host mode
  TEST_ASSERT_EQUAL(USB_ROLE_DRIVE, r.role);
  TEST_ASSERT_TRUE(usb_role_take(&r, USB_ROLE_DRIVE)); // the owner again: fine
  TEST_ASSERT_FALSE(usb_role_take(&r, USB_ROLE_SERIAL)); // serial is not taken, it is what giving back gives
}

void test_host_holds_the_phy_against_the_drive() {
  usb_roles_t r;
  usb_roles_init(&r);
  TEST_ASSERT_TRUE(usb_role_take(&r, USB_ROLE_HOST));
  TEST_ASSERT_FALSE(usb_role_take(&r, USB_ROLE_DRIVE)); // usb_drive_start gets APP_EBUSY
  TEST_ASSERT_EQUAL(USB_ROLE_HOST, r.role);
}

void test_only_the_owner_switches_back() {
  usb_roles_t r;
  usb_roles_init(&r);
  TEST_ASSERT_FALSE(usb_role_give(&r, USB_ROLE_HOST)); // nobody held it
  usb_role_take(&r, USB_ROLE_DRIVE);
  TEST_ASSERT_FALSE(usb_role_give(&r, USB_ROLE_HOST)); // keyboard's device-mode step: the drive keeps its PHY
  TEST_ASSERT_EQUAL(USB_ROLE_DRIVE, r.role);
  TEST_ASSERT_TRUE(usb_role_give(&r, USB_ROLE_DRIVE));
  TEST_ASSERT_EQUAL(USB_ROLE_SERIAL, r.role);
  TEST_ASSERT_TRUE(usb_role_take(&r, USB_ROLE_HOST)); // free again
}

void test_no_host_mode_while_the_drive_has_the_port() {
  usb_port_input_t in = {true, USB_POWER_NO, false, true};
  for (uint32_t t = 0; t < 30000; t += USB_PORT_POLL_MS, now += USB_PORT_POLL_MS) {
    TEST_ASSERT_EQUAL(USB_PORT_DEVICE, usb_port_step(&p, now, &in));
  }
  TEST_ASSERT_EQUAL(USB_WHY_PORT_TAKEN, p.why);
  in.port_taken = false; // the drive ended: host mode after the usual second on battery (already clear)
  TEST_ASSERT_EQUAL(0, host_after(5000, true, USB_POWER_NO, false));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_and_stays_in_device_mode_without_an_app);
  RUN_TEST(test_never_host_while_powered_or_unreadable);
  RUN_TEST(test_host_only_after_a_second_without_input_power);
  RUN_TEST(test_a_power_blip_restarts_the_second);
  RUN_TEST(test_keyboard_keeps_host_mode);
  RUN_TEST(test_no_keyboard_probe_ends_and_retries_later);
  RUN_TEST(test_keyboard_removed_mid_typing_gives_device_mode_at_once);
  RUN_TEST(test_charger_plugged_in_while_host_gives_device_mode_at_once);
  RUN_TEST(test_charger_unreadable_while_host_gives_device_mode_at_once);
  RUN_TEST(test_app_exit_while_host_gives_device_mode_at_once);
  RUN_TEST(test_restart_starts_in_device_mode);
  RUN_TEST(test_clock_wrap);
  RUN_TEST(test_roles_start_serial_and_one_role_at_a_time);
  RUN_TEST(test_host_holds_the_phy_against_the_drive);
  RUN_TEST(test_only_the_owner_switches_back);
  RUN_TEST(test_no_host_mode_while_the_drive_has_the_port);
  return UNITY_END();
}
