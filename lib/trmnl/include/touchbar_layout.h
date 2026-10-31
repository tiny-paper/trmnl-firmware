#pragma once

// Where the X's touch bar is in each position (app_api.h APP_ORIENT_BAR_*), I/O free: the touchbar_layout call of
// the app API, the zones of orientation_follow, and the hint pills (hints.cpp). The bar lies along the panel's
// bottom edge in its own frame (rotation 0), channel 0 under the left third, 1 the middle, 2 the right (docs/apps.md
// "Touch bar").

#include <app_api.h>

// Fills *out for position `orient` on a panel_w x panel_h panel (its own frame, landscape). 0, or APP_EINVAL
int touchbar_layout_fill(int orient, int panel_w, int panel_h, app_touchbar_t *out);
// The user's zone (app_touch_zone_t) of touch channel `channel` (0..2) in position `orient`
int touchbar_zone_of_channel(int orient, int channel);
// A w x h box next to zone `zone`'s edge, centred on the zone, `gap` from the screen's edge, inside the screen
app_rect_t touchbar_hint_box(const app_touchbar_t *bar, int zone, int w, int h, int gap);
// FastEPD's setRotation angle for a position, and back (an unknown angle is APP_ORIENT_BAR_BOTTOM)
int touchbar_rotation_deg(int orient);
int touchbar_orient_of_rotation(int deg);
