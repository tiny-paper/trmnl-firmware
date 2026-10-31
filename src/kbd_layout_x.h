#pragma once

// The X's keyboard layout setting (kbd_layout.h): kept in NVS under KBD_LAYOUT_NVS_KEY (TRMNL's namespace), read once
// and cached; the Wi-Fi page changes it.

int kbd_layout_x_current(void); // KBD_LAYOUT_*; the default when nothing (or an unknown code) is stored
bool kbd_layout_x_store(int layout); // false for a bad value or a failed write (the cached one is unchanged then)
