#pragma once

// The battery and local time calls of the app API (app_api.h battery_percent, local_time), I/O free.

#include <app_api.h>
#include <hardware_types.h>
#include <stdint.h>

// The clock counts as set from late 2023 on (before: never set since power came back)
#define APP_CLOCK_SET_AFTER 1700000000

// `soc`: the fuel gauge's last reading (< 0: unreadable); returns 0..100 or APP_EIO. *charging (may be NULL)
int app_battery_percent(int soc, ChargingStatus status, bool *charging);

// `now`: Unix seconds; `tz`: a POSIX TZ string ("CET-1CEST,M3.5.0,M10.5.0/3"), NULL or "" for none. No zone, or one
// the C library cannot use: UTC with zone_known 0. Returns 0, APP_ENOCLOCK or APP_EINVAL. Sets the process's TZ.
int app_local_time(int64_t now, const char *tz, app_local_time_t *out);
