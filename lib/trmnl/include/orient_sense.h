#pragma once

// The X's position from accelerometer samples, I/O free (accelerometer.cpp reads the BMA530, orientation_x.cpp feeds
// this while an app runs). The angle and the flat gate are TRMNL's (feature/accelerometer_logic, 0bd8762, GoReNeY):
// phi = atan2(-ay, ax) per the BMA530 datasheet eq. 4.5, plus the die's 45 degree mount on the X's board; flat when
// 64 (ax^2 + ay^2) < theta_raw az^2. Positions are app_api.h's APP_ORIENT_BAR_*.

#include <stdbool.h>
#include <stdint.h>

#define ORIENT_FLAT -1
#define ORIENT_NONE -2 // no position yet
#define ORIENT_MOUNT_OFFSET_DEG 45.0f
#define ORIENT_THETA_RAW 0x27 // the BMA530's ORIENTATION_1.theta default (about 38 degrees of tilt)
#define ORIENT_HYSTERESIS_DEG 15 // past a sector's edge before it leaves the position it is in
#define ORIENT_SETTLE_MAX_MS 10000

// Too flat to trust the in-plane angle
bool orient_is_flat(int32_t ax, int32_t ay, int32_t az);
// The mount-corrected in-plane angle, [0, 360)
float orient_corrected_deg(int32_t ax, int32_t ay);
// The position of one sample: ORIENT_FLAT, or the APP_ORIENT_BAR_* whose sector holds the angle. `current` (a
// position or ORIENT_NONE) is kept until the angle is ORIENT_HYSTERESIS_DEG past its sector's edge
int orient_classify(int32_t ax, int32_t ay, int32_t az, int current);

// A new position counts once every sample for settle_ms gave it; flat samples keep the last and restart the wait
typedef struct {
  uint32_t settle_ms;
  int steady; // APP_ORIENT_BAR_*, or ORIENT_NONE before the first non-flat sample
  int candidate; // waiting to become steady, or ORIENT_NONE
  uint32_t since;
} orient_filter_t;

void orient_filter_init(orient_filter_t *f, uint32_t settle_ms);
// One classified sample at now_ms: true when `steady` changed (the first non-flat sample counts at once)
bool orient_filter_feed(orient_filter_t *f, int sample, uint32_t now_ms);
