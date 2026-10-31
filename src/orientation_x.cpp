// The X's position while an app runs (orientation_x.h)
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <accelerometer.h>
#include <app_api.h>
#include <iqs323_task.h>
#include <orient_sense.h>
#include <trmnl_log.h>

#include "orientation_x.h"

static struct bma5_dev dev;
static bool tried, ready;
static int fails;
static orient_filter_t filter = {APP_ORIENT_SETTLE_MS, ORIENT_NONE, ORIENT_NONE, 0};
static volatile bool following;
static uint32_t last_read;

void orientation_x_begin(void) {
  following = false;
  filter.settle_ms = APP_ORIENT_SETTLE_MS; // the steady position is kept: the X did not move by starting an app
  if (tried) return;
  tried = true;
  ready = bma530_wake_and_init(&dev) == BMA5_OK;
  fails = 0;
  Log_info("Orientation: accelerometer %s", ready ? "ready" : "not available, positions from TRMNL's image");
}

int orientation_x_poll(uint32_t now_ms) {
  if (!ready || now_ms - last_read < ORIENTATION_X_POLL_MS) return -1;
  last_read = now_ms;
  struct bma5_accel a;
  iqs323_task_i2c_lock();
  int8_t rc = bma5_get_acc(&a, &dev);
  iqs323_task_i2c_unlock();
  if (rc != BMA5_OK) {
    if (++fails >= ORIENTATION_X_MAX_FAILS) {
      ready = false;
      Log_error("Orientation: %d reads failed (%d), accelerometer off until the next wake", fails, rc);
    }
    return -1;
  }
  fails = 0;
  if (!orient_filter_feed(&filter, orient_classify(a.x, a.y, a.z, filter.steady), now_ms)) return -1;
  Log_info("Orientation: %d (APP_ORIENT_BAR_*)", filter.steady);
  return filter.steady;
}

int orientation_x_steady(void) { return filter.steady; }

int orientation_x_follow(bool on, uint32_t settle_ms) {
  if (settle_ms > ORIENT_SETTLE_MAX_MS) return APP_EINVAL;
  filter.settle_ms = settle_ms ? settle_ms : APP_ORIENT_SETTLE_MS;
  following = on;
  return 0;
}

bool orientation_x_following(void) { return following; }

void orientation_x_end(void) { following = false; }

#endif // BOARD_TRMNL_X
