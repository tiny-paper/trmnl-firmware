#pragma once

// The X's position from its BMA530 while an app runs (app_api.h API 2.5): read by the runner's loop, decided by
// orient_sense.h, asked for by the app's calls. One app at a time.

#include <stdbool.h>
#include <stdint.h>

// At an app's start: the sensor brought up (once per wake), no following, the default settle time
void orientation_x_begin(void);
// From the runner's loop: reads the sensor at most every ORIENTATION_X_POLL_MS; returns the new steady position
// (APP_ORIENT_BAR_*) when it changed, else -1
int orientation_x_poll(uint32_t now_ms);
// The steady position, or ORIENT_NONE (no reading yet, or no sensor)
int orientation_x_steady(void);
// The app's orientation_follow: APP_EINVAL for a settle time over ORIENT_SETTLE_MAX_MS, else 0
int orientation_x_follow(bool on, uint32_t settle_ms);
bool orientation_x_following(void);
// At the app's end
void orientation_x_end(void);

#define ORIENTATION_X_POLL_MS 100
#define ORIENTATION_X_MAX_FAILS 20 // reads failing in a row: the sensor is left alone until the next wake
