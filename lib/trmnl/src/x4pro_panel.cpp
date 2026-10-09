#include <string.h>
#include <x4pro_panel.h>

// The status byte is driven (not just the pull-up or a low line) and shows BUSY_N high (idle)
static bool x4pro_flags_driven(uint8_t u8Flags) { return (u8Flags != 0x00 && u8Flags != 0xff && (u8Flags & 1)); }

bool x4pro_uc_answer(const uint8_t *pRev, uint8_t u8Flags) {
  bool bSame = true;
  for (int i = 1; i < 5; i++) {
    if (pRev[i] != pRev[0]) bSame = false;
  }
  return (x4pro_flags_driven(u8Flags) && !bSame);
}

int x4pro_factory_controller(int iScreenType) {
  if (iScreenType < 0) return X4PRO_CTRL_UNKNOWN;
  if (iScreenType == 1 || iScreenType == 0x0b) return X4PRO_CTRL_UC8179;
  if (iScreenType == 2 || iScreenType == 0x0c) return X4PRO_CTRL_UC8279;
  return X4PRO_CTRL_SSD1677;
}

int x4pro_classify_controller(const uint8_t *pRev1, uint8_t u8Flags1, const uint8_t *pRev2, uint8_t u8Flags2,
                              int iFactoryCtrl, bool *pConclusive) {
  bool bPass1 = x4pro_uc_answer(pRev1, u8Flags1);
  bool bPass2 = x4pro_uc_answer(pRev2, u8Flags2);

  *pConclusive = true;
  if (bPass1 && bPass2 && memcmp(pRev1, pRev2, 5) == 0) {
    switch (pRev2[2]) { // the LUT version tells the two UltraChip parts apart
    case 0x02:
    case 0x03:
    case 0x67:
    case 0x68:
    case 0x69:
      return X4PRO_CTRL_UC8279;
    default:
      return X4PRO_CTRL_UC8179;
    }
  }
  if (!bPass1 && !bPass2 && !x4pro_flags_driven(u8Flags1) && !x4pro_flags_driven(u8Flags2)) {
    return X4PRO_CTRL_SSD1677; // nothing answered
  }
  *pConclusive = false;
  return (iFactoryCtrl != X4PRO_CTRL_UNKNOWN) ? iFactoryCtrl : X4PRO_CTRL_SSD1677;
}

const char *x4pro_controller_name(int iCtrl) {
  switch (iCtrl) {
  case X4PRO_CTRL_SSD1677:
    return "SSD1677";
  case X4PRO_CTRL_UC8179:
    return "UC8179";
  case X4PRO_CTRL_UC8279:
    return "UC8279";
  default:
    return "unknown";
  }
}

int x4pro_gauge_millivolts(uint8_t u8High, uint8_t u8Low) {
  int iCounts = ((u8High & 0x3f) << 8) | u8Low;
  int iMillivolts = (iCounts * 5 + 8) / 16;
  if (iMillivolts < 2500 || iMillivolts > 5000) return -1;
  return iMillivolts;
}
