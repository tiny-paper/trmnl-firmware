#ifdef BOARD_TRMNL_X

#include <display_widgets.h>
#include <epd_driver.h>
#include <hints.h>
#include <stdio.h>
#include <touchbar_layout.h>

#include "fonts.h"

#define HINT_PAD_X    56 // around a hint, inside its pill
#define HINT_PAD_Y    18
#define HINT_BORDER   3  // the pill's outline
#define HINT_ICON_GAP 16 // between a hint's icon and its label

// Same x positions as the TRMNL X's touchbar pills (the panel's own frame; other rotations: touchbar_hint_box)
int hints_slot_x(int slot) { return bbep.width() * (2 * slot + 1) / 6; }

// Fill a triangle pointing in the direction dir (0 = right, 1 = up, 2 = down) centered on (cx, cy).
// size is its extent along the base; the tip is size/2 from the base.
void hints_fill_triangle(int cx, int cy, int size, int dir, uint8_t color) {
  int half = size / 2;
  for (int i = 0; i <= half; i++) {
    int len = size - 2 * i; // length of the line i pixels from the base
    switch (dir) {
    case 0:
      bbep.fillRect(cx - half / 2 + i, cy - len / 2, 1, len, color);
      break;
    case 1:
      bbep.fillRect(cx - len / 2, cy + half / 2 - i, len, 1, color);
      break;
    case 2:
      bbep.fillRect(cx - len / 2, cy - half / 2 + i, len, 1, color);
      break;
    }
  }
}

void hints_draw(const char *label, hints_icon_t icon, int slot, hints_style_t style) {
  hints_draw_at(label, icon, slot, style, bbep.height() - HINTS_MARGIN);
}

void hints_draw_long_press(const char *what, int baseline) {
  char text[64];
  snprintf(text, sizeof(text), "Long press: %s", what);
  hints_draw_at(text, HINTS_ICON_NONE, 1, HINTS_OUTLINED, baseline);
}

// Where the touch bar is in bbep's rotation (touchbar_layout.h): the panel's own frame is rotation 0
static int bar_orientation(app_touchbar_t *bar) {
  const int o = touchbar_orient_of_rotation(bbep.getRotation());
  const bool portrait = o == APP_ORIENT_BAR_LEFT || o == APP_ORIENT_BAR_RIGHT;
  const int panel_w = portrait ? bbep.height() : bbep.width(), panel_h = portrait ? bbep.width() : bbep.height();
  touchbar_layout_fill(o, panel_w, panel_h, bar);
  return o;
}

void hints_draw_at(const char *label, hints_icon_t icon, int slot, hints_style_t style, int y) {
  if (!label) return;
  BB_RECT rect, line;
  bbep.setFont(Inter_18);
  bbep.setCursor(0, y);
  bbep.getStringBox(label, &rect);
  bbep.getStringBox("Ag", &line); // the same height for every hint's pill
  const int tri = icon != HINTS_ICON_NONE ? line.h * 2 / 3 : 0;
  const int content_w = rect.w + (tri ? tri + HINT_ICON_GAP : 0);
  const int pw = content_w + 2 * HINT_PAD_X, ph = line.h + 2 * HINT_PAD_Y;
  int left = hints_slot_x(slot) - content_w / 2;
  int px = left - HINT_PAD_X, py = line.y - HINT_PAD_Y;
  app_touchbar_t bar;
  if (bar_orientation(&bar) != APP_ORIENT_BAR_BOTTOM) {
    // the same pill next to the bar's zone: `y` says how far from the bar's edge, as it does at the bottom
    const int ascent = y - line.y, row = bbep.height() - y; // row: the baseline's distance from the bottom edge
    const int gap = row - (line.h - ascent) - HINT_PAD_Y;
    const bool side = bar.edge == APP_EDGE_LEFT || bar.edge == APP_EDGE_RIGHT;
    // along a side edge a second row goes below the first, not into the screen
    app_rect_t box = touchbar_hint_box(&bar, slot, pw, ph, side ? HINTS_MARGIN : gap);
    if (side && row > HINTS_MARGIN) box.y = (int16_t)(box.y + row - HINTS_MARGIN + ph);
    px = box.x;
    py = box.y;
    left = px + HINT_PAD_X;
    y = py + HINT_PAD_Y + ascent;
    line.y = (int16_t)(py + HINT_PAD_Y);
  }
  const int b = HINT_BORDER;
  const bool filled = style == HINTS_FILLED;
  const uint8_t fg = filled ? BBEP_WHITE : BBEP_BLACK, bg = filled ? BBEP_BLACK : BBEP_WHITE;
  if (style != HINTS_PLAIN) {
    display_fill_pill(px, py, pw, ph, BBEP_BLACK);
    display_fill_pill(px + b, py + b, pw - 2 * b, ph - 2 * b, bg);
  }
  if (tri) {
    hints_fill_triangle(left + tri / 2, line.y + line.h / 2, tri, icon == HINTS_ICON_UP ? 1 : 2, fg);
  }
  bbep.setTextColor(fg, bg);
  bbep.drawString(label, left + content_w - rect.w, y);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
}

#endif
