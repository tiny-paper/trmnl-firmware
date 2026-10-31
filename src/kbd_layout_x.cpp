// The X's keyboard layout setting (kbd_layout_x.h)
#ifdef BOARD_TRMNL_X

#include <globals.h>
#include <kbd_layout.h>

#include "kbd_layout_x.h"

static volatile int cached = -1;

int kbd_layout_x_current(void) {
  if (cached < 0) {
    int l = kbd_layout_from_code(preferences.getString(KBD_LAYOUT_NVS_KEY, "").c_str());
    cached = l < 0 ? KBD_LAYOUT_DEFAULT : l;
  }
  return cached;
}

bool kbd_layout_x_store(int layout) {
  if (layout < 0 || layout >= KBD_LAYOUT_COUNT) return false;
  if (preferences.putString(KBD_LAYOUT_NVS_KEY, kbd_layout_code(layout)) == 0) return false;
  cached = layout;
  return true;
}

#endif // BOARD_TRMNL_X
