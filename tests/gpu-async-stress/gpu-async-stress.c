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

// Ordering stress for a renderer that runs behind the command stream.
//
// Every test queues a long run of GP0 work with no wait in between, then
// reads VRAM or GPUSTAT back and compares against a model kept on the CPU.
// A renderer that lets a readback overtake the draws before it, reorders
// draws against uploads or VRAM copies, or reports GPUSTAT for a texpage it
// has not consumed yet, fails here. The expectations are computed, not
// captured: rectangles, uploads and copies are exact on the PS1.

#include "common/hardware/dma.h"
#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

#include "raster-helpers.h"

CESTER_BODY(
static int s_interruptsWereEnabled;

static uint32_t s_seed;
static uint32_t stressRand(void) {
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

// A 16x16 grid of 8x8 cells at (STRESS_X, STRESS_Y). Every write below
// covers whole cells, so one value per cell is the complete model.
#define STRESS_X 256
#define STRESS_Y 256
#define STRESS_CELLS 16
static uint16_t s_model[STRESS_CELLS * STRESS_CELLS];

static void modelFill(int cx, int cy, int cw, int ch, uint16_t value) {
    for (int y = cy; y < cy + ch; y++) {
        for (int x = cx; x < cx + cw; x++) s_model[y * STRESS_CELLS + x] = value;
    }
}

static void stressReset(void) {
    rasterReset();
    rasterFillRect(STRESS_X, STRESS_Y, STRESS_CELLS * 8, STRESS_CELLS * 8, 0);
    modelFill(0, 0, STRESS_CELLS, STRESS_CELLS, 0);
}

// Random 5:5:5 colour with bit 15 clear, and the command word that draws it.
static uint16_t stressColor(uint32_t *cmd) {
    uint8_t r = stressRand() & 0x1f, g = stressRand() & 0x1f, b = stressRand() & 0x1f;
    *cmd = rasterCmdColor(r, g, b);
    return rasterVram555(r, g, b);
}

static void vramCopy(int16_t sx, int16_t sy, int16_t dx, int16_t dy, int16_t w, int16_t h) {
    waitGPU();
    GPU_DATA = 0x80000000u;
    GPU_DATA = ((uint32_t)(uint16_t)sy << 16) | (uint32_t)(uint16_t)sx;
    GPU_DATA = ((uint32_t)(uint16_t)dy << 16) | (uint32_t)(uint16_t)dx;
    GPU_DATA = ((uint32_t)(uint16_t)h << 16) | (uint32_t)(uint16_t)w;
}

// Reads one pixel per cell and counts the cells that disagree with the model.
static int stressCheck(void) {
    int bad = 0;
    for (int cy = 0; cy < STRESS_CELLS; cy++) {
        uint16_t row[STRESS_CELLS * 8];
        rasterReadStrip(STRESS_X, STRESS_Y + cy * 8 + 3, STRESS_CELLS * 8, row);
        for (int cx = 0; cx < STRESS_CELLS; cx++) {
            uint16_t expect = s_model[cy * STRESS_CELLS + cx];
            uint16_t got = row[cx * 8 + 5];
            if (got != expect) {
                if (bad < 8) ramsyscall_printf("cell %d,%d: got 0x%04x expect 0x%04x\n", cx, cy, got, expect);
                bad++;
            }
        }
    }
    return bad;
}
)

CESTER_BEFORE_ALL(gpu_async_stress,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
)

CESTER_AFTER_ALL(gpu_async_stress,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

// A readback issued right after a draw must see that draw, every time.
CESTER_TEST(readback_after_each_draw, gpu_async_stress,
    stressReset();
    s_seed = 1;
    int bad = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t cmd;
        uint16_t color = stressColor(&cmd);
        int16_t x = STRESS_X + (stressRand() % STRESS_CELLS) * 8;
        int16_t y = STRESS_Y + (stressRand() % STRESS_CELLS) * 8;
        rasterFlatRect(cmd, x, y, 8, 8);
        if (rasterReadPixel(x + 4, y + 4) != color) bad++;
    }
    cester_assert_int_eq(0, bad);
)

// Thousands of overlapping rectangles queued back to back, then one check.
CESTER_TEST(long_queue_of_rects, gpu_async_stress,
    stressReset();
    s_seed = 2;
    for (int i = 0; i < 4000; i++) {
        uint32_t cmd;
        uint16_t color = stressColor(&cmd);
        int cx = stressRand() % STRESS_CELLS, cy = stressRand() % STRESS_CELLS;
        int cw = 1 + stressRand() % (STRESS_CELLS - cx), ch = 1 + stressRand() % (STRESS_CELLS - cy);
        rasterFlatRect(cmd, STRESS_X + cx * 8, STRESS_Y + cy * 8, cw * 8, ch * 8);
        modelFill(cx, cy, cw, ch, color);
    }
    cester_assert_int_eq(0, stressCheck());
)

// Draws, CPU uploads, fills and VRAM-to-VRAM copies interleaved. The copies
// read pixels the queued draws just wrote, and later draws overwrite their
// source, so any reordering between the four kinds shows up.
CESTER_TEST(mixed_draw_upload_copy, gpu_async_stress,
    stressReset();
    s_seed = 3;
    for (int i = 0; i < 1500; i++) {
        uint32_t cmd;
        uint16_t color = stressColor(&cmd);
        int cx = stressRand() % STRESS_CELLS, cy = stressRand() % STRESS_CELLS;
        int cw = 1 + stressRand() % (STRESS_CELLS - cx), ch = 1 + stressRand() % (STRESS_CELLS - cy);
        if (cw > 4) cw = 4;
        if (ch > 4) ch = 4;
        switch (stressRand() % 4) {
            case 0:
                rasterFlatRect(cmd, STRESS_X + cx * 8, STRESS_Y + cy * 8, cw * 8, ch * 8);
                modelFill(cx, cy, cw, ch, color);
                break;
            case 1:
                rasterFillRect(STRESS_X + cx * 8, STRESS_Y + cy * 8, cw * 8, ch * 8, color);
                modelFill(cx, cy, cw, ch, color);
                break;
            case 2:
                // GP0(0x02) fast fill, 16-pixel granular in X: two cells.
                cx &= ~1;
                waitGPU();
                GPU_DATA = 0x02000000u | cmd;
                GPU_DATA = ((uint32_t)(STRESS_Y + cy * 8) << 16) | (uint32_t)(STRESS_X + cx * 8);
                GPU_DATA = ((uint32_t)(ch * 8) << 16) | 16u;
                modelFill(cx, cy, 2, ch, color);
                break;
            case 3: {
                int dx = stressRand() % (STRESS_CELLS - cw + 1), dy = stressRand() % (STRESS_CELLS - ch + 1);
                // Overlapping copies depend on the copy's walk order; keep this about ordering.
                if ((dx < cx + cw) && (cx < dx + cw) && (dy < cy + ch) && (cy < dy + ch)) break;
                vramCopy(STRESS_X + cx * 8, STRESS_Y + cy * 8, STRESS_X + dx * 8, STRESS_Y + dy * 8, cw * 8, ch * 8);
                uint16_t tmp[16];
                for (int y = 0; y < ch; y++) {
                    for (int x = 0; x < cw; x++) tmp[y * 4 + x] = s_model[(cy + y) * STRESS_CELLS + cx + x];
                }
                for (int y = 0; y < ch; y++) {
                    for (int x = 0; x < cw; x++) s_model[(dy + y) * STRESS_CELLS + dx + x] = tmp[y * 4 + x];
                }
                break;
            }
        }
        // An occasional single-pixel readback in the middle of the stream.
        if ((i % 97) == 0) {
            int px = stressRand() % STRESS_CELLS, py = stressRand() % STRESS_CELLS;
            if (rasterReadPixel(STRESS_X + px * 8 + 1, STRESS_Y + py * 8 + 1) != s_model[py * STRESS_CELLS + px]) {
                cester_assert_int_eq(-1, i);
            }
        }
    }
    cester_assert_int_eq(0, stressCheck());
)

// GPUSTAT reflects E1 and E6 once the GPU can take a command again.
CESTER_TEST(status_follows_env_commands, gpu_async_stress,
    stressReset();
    s_seed = 4;
    int bad = 0;
    for (int i = 0; i < 500; i++) {
        uint32_t cmd;
        stressColor(&cmd);
        rasterFlatRect(cmd, STRESS_X, STRESS_Y, STRESS_CELLS * 8, STRESS_CELLS * 8);
        uint32_t tpage = stressRand() & 0x1ff;  // texpage bits 0-8, dither and draw-to-display off
        uint32_t mask = stressRand() & 3;
        sendGPUData(0xe1000000u | tpage);
        sendGPUData(0xe6000000u | mask);
        // On hardware E1 and E6 sit in the FIFO behind the rectangle until
        // GPUSTAT bit 26 comes back. The soft GPU keeps bit 26 set, so this
        // wait does not hold for its worker thread.
        waitGPU();
        uint32_t stat = GPU_STATUS;
        if ((stat & 0x7ff) != tpage) bad++;
        if (((stat >> 11) & 3) != mask) bad++;
    }
    sendGPUData(0xe6000000u);
    sendGPUData(0xe1000400u);
    cester_assert_int_eq(0, bad);
)
