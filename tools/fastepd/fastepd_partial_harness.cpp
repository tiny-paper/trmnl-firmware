// SPDX-License-Identifier: MIT
// Host harness for FastEPD's 1 bit partial update (test_fastepd_partial.py builds and runs it), from tiny-paper
// tools/panel: the library's own bbepPartialUpdate and 1 bit lookup tables, cut from src/FastEPD.inl into partial.inc
// and luts.inc, over stubs that record what would go to the glass (rails, scans, the 2 bit codes of every row).
// Each case is compared with the model below: codes 01 black, 10 white, 00 no drive.
//   pp scans    in [y0,y1] a pixel white before and black now 01, black before and white now 10, else 00;
//               every row outside [y0,y1] all 00 (not driven) on every pass
//   then one neutral scan, the rails down unless kept on, and the previous frame equal to the current one.
#define __LINUX__
#define __MEM_ONLY__
#include "FastEPD.h"
#include <string>
#include <vector>

static std::vector<std::string> g_ev; // "P1", "P0", "S" (scan start), "R<bytes>" (a row), "N" (neutral scan)
static uint16_t LUTW_16[256], LUTB_16[256], LUTBW_16[256];

static void bbepConvertPrevBuffer(FASTEPDSTATE *) { g_ev.push_back("convert"); }
static int bbepEinkPower(FASTEPDSTATE *, int bOn) { g_ev.push_back(bOn ? "P1" : "P0"); return BBEP_SUCCESS; }
static void it8951WriteFramebuffer1Bit(FASTEPDSTATE *, bool, int, int) { g_ev.push_back("it8951"); }
static int bbepFullUpdate(FASTEPDSTATE *, int, bool, BB_RECT *) { g_ev.push_back("full"); return BBEP_SUCCESS; }
static int bbep2BppPartial(FASTEPDSTATE *, bool, int, int) { g_ev.push_back("2bpp"); return BBEP_SUCCESS; }
static void bbepRowControl(FASTEPDSTATE *, int iMode) { g_ev.push_back(iMode == ROW_START ? "S" : "rowctl"); }
static void bbepWriteRow(FASTEPDSTATE *, uint8_t *pData, int iLen, int)
{
    g_ev.push_back("R" + std::string((const char *)pData, (size_t)iLen));
}
static void bbepClear(FASTEPDSTATE *, uint8_t val, uint8_t count, BB_RECT *pRect)
{
    g_ev.push_back(val == BB_CLEAR_NEUTRAL && count == 1 && !pRect ? "N" : "clear?");
}

#include "partial.inc"

static void make_luts(FASTEPDSTATE *pState)
{
#include "luts.inc"
}

enum { W = 48, H = 7, PP = 3 };

static int px(const uint8_t *f, int x, int y) { return (f[y * (W / 8) + x / 8] >> (7 - (x & 7))) & 1; } // 1: white

static int run(int flags, int y0, int y1, bool keep)
{
    static uint8_t cur[W / 8 * H], prev[W / 8 * H], was[W / 8 * H], temp[W / 4 * H], dma[W / 2];
    FASTEPDSTATE st;
    memset(&st, 0, sizeof st);
    st.native_width = st.width = W;
    st.native_height = st.height = H;
    st.mode = st.prev_mode = BB_MODE_1BPP;
    st.iPanelType = BB_PANEL_EPDIY_V7;
    st.iFlags = flags;
    st.pCurrent = cur;
    st.pPrevious = prev;
    st.pTemp = temp;
    st.dma_buf = dma;
    st.iPartialPasses = PP;
    unsigned seed = 12345u + (unsigned)flags;
    for (size_t i = 0; i < sizeof cur; i++) {
        seed = seed * 1103515245u + 12345u;
        cur[i] = (uint8_t)(seed >> 16);
        prev[i] = (uint8_t)(seed >> 24);
    }
    memcpy(was, prev, sizeof was);
    memset(temp, 0x55, sizeof temp); // rows outside the range: an older call's codes, never to be sent
    memset(dma, 0xff, sizeof dma);
    make_luts(&st);
    g_ev.clear();
    if (bbepPartialUpdate(&st, keep, y0, y1) != BBEP_SUCCESS) return printf("partial update failed\n");

    std::vector<std::string> want;
    want.push_back("P1");
    for (int pass = 0; pass < PP; pass++) {
        want.push_back("S");
        for (int i = 0; i < H; i++) {
            const int y = (flags & BB_PANEL_FLAG_MIRROR_Y) ? H - 1 - i : i;
            std::string row((size_t)(W / 4), '\0');
            for (int x = 0; x < W && y >= y0 && y <= y1; x++) {
                const int p = px(was, x, y), c = px(cur, x, y);
                const int code = !p && c ? 2 : p && !c ? 1 : 0;
                const int at = (flags & BB_PANEL_FLAG_MIRROR_X) ? W - 1 - x : x;
                row[(size_t)(at / 4)] = (char)(row[(size_t)(at / 4)] | (code << (6 - 2 * (at & 3))));
            }
            want.push_back("R" + row);
        }
    }
    want.push_back("N");
    if (!keep) want.push_back("P0");
    if (g_ev != want) {
        size_t i = 0;
        while (i < g_ev.size() && i < want.size() && g_ev[i] == want[i]) i++;
        return printf("event %zu of %zu differs (%zu expected)\n", i, g_ev.size(), want.size());
    }
    for (int y = y0; y <= y1; y++)
        if (memcmp(prev + y * (W / 8), cur + y * (W / 8), W / 8)) return printf("row %d: previous is not current\n", y);
    return 0;
}

int main(void)
{
    static const int FLAGS[] = { 0, BB_PANEL_FLAG_MIRROR_X, BB_PANEL_FLAG_MIRROR_Y,
                                 BB_PANEL_FLAG_MIRROR_X | BB_PANEL_FLAG_MIRROR_Y };
    int bad = 0, n = 0;
    for (int f = 0; f < 4; f++)
        for (int r = 0; r < 2; r++)
            for (int k = 0; k < 2; k++, n++) {
                const int y0 = r ? 2 : 0, y1 = r ? 4 : H - 1;
                printf("flags=%d rows=%d..%d keep=%d: ", FLAGS[f], y0, y1, k);
                if (run(FLAGS[f], y0, y1, k != 0)) bad++;
                else printf("ok\n");
            }
    printf("%d of %d cases failed\n", bad, n);
    return bad ? 1 : 0;
}
