/*

MIT License

Copyright (c) 2026 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

// Test: CLUT Y bit 9 (CLUT word bit 15) and texpage Y bit 11 follow the
// GP1(09h) bank gate when a textured primitive samples through them.
//
// With the gate closed every VRAM access wraps Y at 9 bits, so a CLUT word
// of 0xF800 (Y=992) must read the palette at row 480, and a texture page
// selected through texpage bit 11 must read the lower bank. Tales of Destiny
// issues exactly that CLUT word for its town tiles with the gate closed.
//
// Pass "clut-480" draws with the palette addressed directly and validates the
// setup. Passes "clut-992" and "texpage-bit11" are the gate-closed folds. On a
// 2MB fitment "clut-992-open" re-runs the CLUT read with the gate open and
// expects the palette stored in the real upper bank at row 992.

#include "probe-common.h"

#define DEST_X 16
#define DEST_Y 16
#define TILE 8
#define PAGE_X 64  // texpage tx=1
#define LOW_COLOR 0x001fu
#define HIGH_COLOR 0x03e0u

// CLUT entry 1 at (0,480), and on a 2MB fitment a different entry 1 at
// (0,992), written with the gate open so it lands in the upper bank.
static void setupPalettes(void) {
    gpuFullResetWithGate(1);
    writePixel(0, 992, 0);
    writePixel(1, 992, HIGH_COLOR);
    gpuFullResetWithGate(0);
    writePixel(0, 480, 0);
    writePixel(1, 480, LOW_COLOR);
    // A 4bpp TILE x TILE texture whose every texel is index 1.
    fillRectViaUpload(PAGE_X, 0, TILE / 4, TILE, 0x1111u);
}

static int upperBankFitted(void) {
    gpuFullResetWithGate(1);
    writePixel(200, 0, 0x1234);
    writePixel(200, 512, 0x4321);
    int fitted = readPixel(200, 0) == 0x1234 && readPixel(200, 512) == 0x4321;
    gpuFullResetWithGate(0);
    return fitted;
}

static void onePass(ProbeStats* stats, const char* label, int gate, uint16_t clut, int bit11, uint16_t want) {
    gpuFullResetWithGate(gate);
    sendGPUData(0xe3000000u);
    sendGPUData(0xe4000000u | ((uint32_t)511 << 10) | 1023);
    sendGPUData(0xe5000000u);
    fillRectViaUpload(DEST_X, DEST_Y, TILE, TILE, 0x7fffu);
    // GP0(E1h): tx=1, ty=0, 4bpp, plus texpage bit 11.
    sendGPUData(0xe1000000u | 1u | ((uint32_t)(bit11 & 1) << 11));
    waitGPU();
    GPU_DATA = 0x64808080u;
    GPU_DATA = ((uint32_t)DEST_Y << 16) | (uint32_t)DEST_X;
    GPU_DATA = (uint32_t)clut << 16;  // U=0, V=0
    GPU_DATA = ((uint32_t)TILE << 16) | (uint32_t)TILE;

    int matches = 0;
    uint16_t buf[TILE];
    uint16_t first = 0;
    for (int row = 0; row < TILE; row++) {
        readStrip(DEST_X, DEST_Y + row, TILE, buf);
        if (row == 0) first = buf[0];
        for (int u = 0; u < TILE; u++) {
            if ((buf[u] & 0x7fff) == want) matches++;
        }
    }
    PROBE_RESULT("clut-y-fold %s gate=%d clut=%04x bit11=%d want=%04x got=%04x matches=%d/%d", label, gate, clut, bit11,
                 want, first, matches, TILE * TILE);
    if (matches == TILE * TILE) {
        PROBE_PASS(stats, "%s", label);
    } else {
        PROBE_FAIL(stats, "%s got=%04x want=%04x", label, first, want);
    }
    waitGPU();
}

int main(void) {
    ramsyscall_printf("\n=== clut-y-fold ===\n");
    probeReset();

    ProbeStats stats;
    probeStatsInit(&stats);

    int fitted = upperBankFitted();
    setupPalettes();
    PROBE_INFO(&stats, "upper bank %s", fitted ? "fitted" : "absent");

    onePass(&stats, "clut-480", 0, 480 << 6, 0, LOW_COLOR);
    onePass(&stats, "clut-992", 0, 992 << 6, 0, LOW_COLOR);
    onePass(&stats, "texpage-bit11", 0, 480 << 6, 1, LOW_COLOR);
    if (fitted) onePass(&stats, "clut-992-open", 1, 992 << 6, 0, HIGH_COLOR);

    PROBE_INFO(&stats, "clut-y-fold sweep complete");
    probeStatsSummary(&stats, "clut-y-fold");
    probeExit(&stats);
    return 0;
}
