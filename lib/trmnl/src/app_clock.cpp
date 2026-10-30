#include "app_clock.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

int app_battery_percent(int soc, ChargingStatus status, bool *charging) {
  if (charging) *charging = status == ChargingStatus::CHARGING;
  if (soc < 0) return APP_EIO;
  return soc > 100 ? 100 : soc;
}

static void fill(const struct tm &t, int32_t offset, bool zone, app_local_time_t *out) {
  memset(out, 0, sizeof(*out));
  out->year = (int16_t)(t.tm_year + 1900);
  out->month = (uint8_t)(t.tm_mon + 1);
  out->day = (uint8_t)t.tm_mday;
  out->hour = (uint8_t)t.tm_hour;
  out->minute = (uint8_t)t.tm_min;
  out->second = (uint8_t)t.tm_sec;
  out->weekday = (uint8_t)t.tm_wday;
  out->utc_offset_s = offset;
  out->zone_known = zone;
  out->dst = zone && t.tm_isdst > 0;
}

// Seconds of `local` minus `utc` (both broken down from the same instant)
static int32_t offset_of(const struct tm &local, const struct tm &utc) {
  long d = (local.tm_yday - utc.tm_yday);
  if (local.tm_year != utc.tm_year) d = local.tm_year > utc.tm_year ? 1 : -1; // a year boundary between them
  return (int32_t)(((d * 24 + (local.tm_hour - utc.tm_hour)) * 60 + (local.tm_min - utc.tm_min)) * 60 +
                   (local.tm_sec - utc.tm_sec));
}

// A POSIX TZ string as far as the libraries agree: a name of 3+ letters (or <...>), then an hour offset. Anything else
// (a city name like "Europe/Brussels", which newlib does not know) is no zone rather than a silent UTC
static bool tz_ok(const char *tz) {
  if (!tz || !*tz || strlen(tz) > 64) return false;
  const char *p = tz;
  if (*p == '<') {
    p = strchr(p, '>');
    if (!p || p - tz < 4) return false;
    p++;
  } else {
    int n = 0;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++, n++;
    if (n < 3) return false;
  }
  if (*p == '+' || *p == '-') p++;
  return *p >= '0' && *p <= '9';
}

int app_local_time(int64_t now, const char *tz, app_local_time_t *out) {
  if (!out) return APP_EINVAL;
  if (now < APP_CLOCK_SET_AFTER) return APP_ENOCLOCK;
  time_t t = (time_t)now;
  struct tm utc, local;
  if (!gmtime_r(&t, &utc)) return APP_ENOCLOCK;
  bool zone = tz_ok(tz);
  if (zone) {
    setenv("TZ", tz, 1);
    tzset();
    zone = localtime_r(&t, &local) != nullptr;
  }
  if (!zone) {
    fill(utc, 0, false, out);
    return 0;
  }
  fill(local, offset_of(local, utc), true, out);
  return 0;
}
