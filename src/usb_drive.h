#pragma once

// The USB drive for the running app (app_api.h version 2, docs/apps.md "USB drive"). start/stop
// come from the app's task, service and end_for_exit from the runner's.

#include <app_api.h>

int usb_drive_start(uint32_t connect_timeout_ms);
int usb_drive_state(void);
int usb_drive_stop(app_drive_result_t *res);

// The runner, every poll: ends the drive on eject, unplug or timeout (imports per the rules)
void usb_drive_service(void);

// The runner, when the app ended: ends a drive still showing, as an unplug would
void usb_drive_end_for_exit(void);

// True while the drive shows or its import runs: the app's file writes are refused
bool usb_drive_busy(void);

// True while an ending showing imports
bool usb_drive_importing(void);

// Finishes an import a power cut interrupted, in the app's folder; before the app starts
void usb_drive_recover(const char *app_dir);
