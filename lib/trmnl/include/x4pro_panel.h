#pragma once
// Xteink X4 Pro: the hardware independent rules (which EPD controller a probe means, fuel gauge scaling)
#include <stdint.h>

// The panel controller depends on the production batch
enum { X4PRO_CTRL_UNKNOWN = 0, X4PRO_CTRL_SSD1677, X4PRO_CTRL_UC8179, X4PRO_CTRL_UC8279 };

// A valid UltraChip (UC81xx/UC82xx) answer to the status (FLG, 0x71) and revision (REV, 0x70) reads:
// the status is driven (not 0x00/0xff) with BUSY_N set, and the 5 revision bytes are not one repeated value
bool x4pro_uc_answer(const uint8_t *pRev, uint8_t u8Flags);

// The controller named by the factory setting "screenType" (pass a negative value if it is absent)
int x4pro_factory_controller(int iScreenType);

// Decide the controller from two probe passes (5 REV bytes and the FLG byte of each).
// Both passes valid and equal: UC8279 or UC8179 by the LUT version (REV byte 2).
// Nothing answered in either pass: SSD1677 (it has no such reads).
// Anything else is not conclusive (*pConclusive = false): the factory controller, else SSD1677.
int x4pro_classify_controller(const uint8_t *pRev1, uint8_t u8Flags1, const uint8_t *pRev2, uint8_t u8Flags2,
                              int iFactoryCtrl, bool *pConclusive);

const char *x4pro_controller_name(int iCtrl);

// CW2017 VCELL registers 2 (high) and 3 (low): 14 bits, 312.5uV per count.
// Returns millivolts, or -1 if the value cannot be a battery (gauge asleep or not a CW2017)
int x4pro_gauge_millivolts(uint8_t u8High, uint8_t u8Low);
