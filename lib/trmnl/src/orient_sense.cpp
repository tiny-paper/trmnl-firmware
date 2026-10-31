#include <app_api.h>
#include <math.h>
#include <orient_sense.h>

bool orient_is_flat(int32_t ax, int32_t ay, int32_t az) {
  int64_t horiz_sq = (int64_t)ax * ax + (int64_t)ay * ay;
  int64_t az_sq = (int64_t)az * az;
  return 64 * horiz_sq < (int64_t)ORIENT_THETA_RAW * az_sq;
}

float orient_corrected_deg(int32_t ax, int32_t ay) {
  float phi = atan2f((float)-ay, (float)ax) * 180.0f / (float)M_PI;
  if (phi < 0) phi += 360.0f;
  float d = phi + ORIENT_MOUNT_OFFSET_DEG;
  if (d >= 360.0f) d -= 360.0f;
  return d;
}

// The angle at the middle of a position's sector. TRMNL's sectors: [135, 225) LANDSCAPE_RIGHT (the panel's own frame,
// rotation 0), [225, 315) PORTRAIT_UP_RIGHT (90), [315, 45) LANDSCAPE_LEFT (180), [45, 135) PORTRAIT_UP_DOWN (270)
static float sector_mid(int pos) { return (float)((180 + 90 * pos) % 360); }

static float distance_deg(float a, float b) {
  float d = fabsf(a - b);
  return d > 180.0f ? 360.0f - d : d;
}

int orient_classify(int32_t ax, int32_t ay, int32_t az, int current) {
  if (orient_is_flat(ax, ay, az)) return ORIENT_FLAT;
  float deg = orient_corrected_deg(ax, ay);
  if (current >= APP_ORIENT_BAR_BOTTOM && current <= APP_ORIENT_BAR_RIGHT &&
      distance_deg(deg, sector_mid(current)) < 45.0f + ORIENT_HYSTERESIS_DEG) {
    return current;
  }
  int best = APP_ORIENT_BAR_BOTTOM;
  for (int pos = APP_ORIENT_BAR_BOTTOM + 1; pos <= APP_ORIENT_BAR_RIGHT; pos++) {
    if (distance_deg(deg, sector_mid(pos)) < distance_deg(deg, sector_mid(best))) best = pos;
  }
  return best;
}

void orient_filter_init(orient_filter_t *f, uint32_t settle_ms) {
  f->settle_ms = settle_ms;
  f->steady = ORIENT_NONE;
  f->candidate = ORIENT_NONE;
  f->since = 0;
}

bool orient_filter_feed(orient_filter_t *f, int sample, uint32_t now_ms) {
  if (sample < APP_ORIENT_BAR_BOTTOM || sample > APP_ORIENT_BAR_RIGHT) { // flat (or a failed read): keep the last
    f->candidate = ORIENT_NONE;
    return false;
  }
  if (f->steady == ORIENT_NONE) {
    f->steady = sample;
    f->candidate = ORIENT_NONE;
    return true;
  }
  if (sample == f->steady) {
    f->candidate = ORIENT_NONE;
    return false;
  }
  if (sample != f->candidate) {
    f->candidate = sample;
    f->since = now_ms;
  }
  if (now_ms - f->since < f->settle_ms) return false;
  f->steady = sample;
  f->candidate = ORIENT_NONE;
  return true;
}
