#pragma once

// The screen_orientation call of the app API (app_api.h), I/O free. TRMNL keeps no orientation on the device: the
// server renders a portrait screen as an image with the panel's sides swapped, and the firmware turns it while
// drawing (png_render.cpp). So the orientation is that of the last TRMNL image drawn.

#include <app_api.h>

// img_w, img_h: the last TRMNL image (0, 0: none since the last cold start); panel_w, panel_h: the panel.
// Returns APP_ORIENT_LANDSCAPE, APP_ORIENT_PORTRAIT or APP_ENOENT (no image yet, or one of another size).
int app_orientation(int img_w, int img_h, int panel_w, int panel_h);

// API 2.5. sensor: the accelerometer's steady position (APP_ORIENT_BAR_*), or < 0 for none yet; from_image:
// app_orientation's result. screen_orientation: the sensor's as landscape (bar bottom or top) or portrait (bar left
// or right), else from_image
int app_orientation_screen(int sensor, int from_image);
// device_orientation: the sensor's, else from_image (landscape is BAR_BOTTOM, portrait BAR_LEFT), else BAR_BOTTOM
int app_orientation_device(int sensor, int from_image);
