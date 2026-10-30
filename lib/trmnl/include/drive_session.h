#pragma once

// One showing of the USB drive (docs/apps.md, "USB drive"): what the computer did, and whether its
// changes are taken when the drive ends. Taken whole or not at all: after an eject, after the
// computer's last write was synced (SCSI SYNCHRONIZE CACHE), or after DRIVE_QUIET_MS without a
// write. A cable pulled while a copy still runs takes nothing (the image may hold half a file).

#include <stdint.h>

#define DRIVE_QUIET_MS 2000

typedef enum {
  DRIVE_END_NONE = 0,
  DRIVE_END_EJECTED, // the computer ejected the drive
  DRIVE_END_UNPLUGGED, // the cable went, or the computer reset the bus, without an eject
  DRIVE_END_TIMEOUT, // no computer came within the app's time
  DRIVE_END_STOPPED, // the app ended it, or the app ended
} drive_end_t;

typedef struct {
  bool mounted; // a computer configured the device
  uint32_t writes; // WRITE(10) commands
  uint32_t last_write_ms;
  bool synced; // a SYNCHRONIZE CACHE came after the last write
} drive_session_t;

void drive_session_reset(drive_session_t *s);
void drive_session_wrote(drive_session_t *s, uint32_t now_ms);
void drive_session_synced(drive_session_t *s);

// True when the image's changes are to be imported
bool drive_session_take(const drive_session_t *s, drive_end_t end, uint32_t now_ms);
