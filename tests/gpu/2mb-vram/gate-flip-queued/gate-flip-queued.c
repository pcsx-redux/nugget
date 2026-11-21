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

// GP1(09h) written while GP0 work is still queued.
//
// GP1 is unbuffered, so the probe waits for GPUSTAT bit 26 before writing the
// gate, and every primitive sent before the write must use the gate value
// that was current when it was sent. With the gate open the probe queues a
// long run of large filler rectangles so the renderer is busy, then a small
// marker rectangle at Y=600, waits for bit 26 and closes the gate. The marker
// must be in the upper bank at physical row 600, and row 88 (600 wrapped to
// 1MB) must be untouched. The first readback runs with the gate still closed.
// Needs -2mbvram.

#include "probe-common.h"

#define COL_X      64
#define COL_W      32
#define MARK_Y     600
#define WRAP_Y     (MARK_Y - 512)
// Command color: B=0xff -> VRAM 0x7c00. Filler: G=0xff -> VRAM 0x03e0.
#define MARK_CMD   0x00ff0000u
#define MARK_VRAM  0x7c00u
#define FILL_CMD   0x0000ff00u
#define BG_COLOR   0x0000u
#define FILLERS    64

static ProbeStats* s_stats = 0;

static void rect(uint32_t color, int x, int y, int w, int h) {
    waitGPU();
    GPU_DATA = 0x60000000u | color;
    GPU_DATA = ((uint32_t)y << 16) | (uint32_t)x;
    GPU_DATA = ((uint32_t)h << 16) | (uint32_t)w;
}

static void setup(void) {
    gpuFullResetWithGate(1);
    fillRectViaUpload(COL_X, WRAP_Y - 8, COL_W, 32, BG_COLOR);
    fillRectViaUpload(COL_X, MARK_Y - 8, COL_W, 32, BG_COLOR);
    sendGPUData(0xe3000000u);
    sendGPUData(0xe4000000u | ((uint32_t)1023 << 10) | 1023u);
    sendGPUData(0xe5000000u);
}

// Queue the fillers and the marker, wait for the GPU, then write the gate.
static void drawThenFlip(uint32_t newGate) {
    for (int i = 0; i < FILLERS; i++) rect(FILL_CMD, 512, 0, 511, 255);
    rect(MARK_CMD, COL_X, MARK_Y, 16, 8);
    waitGPU();
    gp1_09(newGate);
}

static void expectRow(const char* label, int y, int wantMarker) {
    uint16_t px = readPixel(COL_X + 4, y);
    PROBE_RESULT("gate-flip-queued %s y=%d px=%04x", label, y, px);
    if (px == (wantMarker ? MARK_VRAM : BG_COLOR)) {
        PROBE_PASS(s_stats, "%s y=%d %s", label, y, wantMarker ? "marker" : "background");
    } else {
        PROBE_FAIL(s_stats, "%s y=%d px=%04x expected %s", label, y, px, wantMarker ? "marker" : "background");
    }
}

int main(void) {
    ramsyscall_printf("\n=== 573 gate-flip-queued ===\n");
    probeReset();

    ProbeStats stats;
    probeStatsInit(&stats);
    s_stats = &stats;

    setup();
    drawThenFlip(0);
    expectRow("open-then-close", WRAP_Y, 0);
    gp1_09(1);
    expectRow("open-then-close", MARK_Y, 1);

    probeStatsSummary(&stats, "gate-flip-queued");
    probeExit(&stats);
    return 0;
}
