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

/*
 * The shell's 640x480 interlaced frame loop: waitVSync(1), then a single-buffer
 * clear with GP0(E1h).10 clear, as main.c does it. With the bit clear the GPU
 * skips every row whose parity equals GPUSTAT.31 at draw time (see
 * tests/gpu-dfe-480i), so two consecutive frames must land on opposite
 * GPUSTAT.31 values for the clears to cover all 480 rows. VRAM is cleared before
 * the last two frames, so the coverage check sees only those two.
 */

#include <stdint.h>

#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"
#include "shell/gpu.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
#define ROWS 480
#define FRAMES 6

static uint8_t s_rows[ROWS];
static uint32_t s_col[ROWS];
static uint32_t s_bit31[FRAMES];
static int s_interruptsWereEnabled;

static void idle(void) {}

static void waitIdle(void) {
    while ((GPU_STATUS & 0x14000000) != 0x14000000);
}

static void run(void) {
    initGPU(0);
    GPU_DATA = 0xe1000400;
    struct FastFill black = {.c = {.r = 0, .g = 0, .b = 0}, .x = 0, .y = 0, .w = WIDTH, .h = HEIGHT};
    fastFill(&black);
    waitIdle();
    GPU_DATA = 0xe1000000;
    IREG = ~IRQ_VBLANK;
    const union Color white = {.r = 255, .g = 255, .b = 255};
    for (int i = 0; i < FRAMES; i++) {
        waitVSync(1, idle);
        s_bit31[i] = GPU_STATUS >> 31;
        // Only the last two frames count towards coverage: the first wait has no history.
        if (i == FRAMES - 2) {
            GPU_DATA = 0xe1000400;
            fastFill(&black);
            waitIdle();
            GPU_DATA = 0xe1000000;
        }
        flip(0, white);
        waitIdle();
    }
    GPU_DATA = 0x01000000;
    GPU_DATA = 0xc0000000;
    GPU_DATA = 0x00000000;
    GPU_DATA = (ROWS << 16) | 2;
    while (!(GPU_STATUS & 0x08000000));
    for (int i = 0; i < ROWS; i++) s_col[i] = GPU_DATA;
    for (int i = 0; i < ROWS; i++) s_rows[i] = (s_col[i] & 0x7fff7fff) != 0;
}

// Rows 0 and 1 are left out: on a v0 GPU (SCPH-1000) a fill whose first row has the skipped
// parity draws that row and skips the next one, so the 262-line field's clear leaves row 1.
static unsigned count(int parity) {
    unsigned n = 0;
    for (int y = parity + 2; y < ROWS; y += 2) n += s_rows[y];
    return n;
}
)

CESTER_BEFORE_ALL(shell480i,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    run();
    for (int i = 0; i < FRAMES; i++) ramsyscall_printf("frame %d GPUSTAT.31 after wait = %u\n", i, s_bit31[i]);
    ramsyscall_printf("rows 2-479 written: even %u/239, odd %u/239\n", count(0), count(1));
)

CESTER_AFTER_ALL(shell480i,
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(waitsAlternateBit31, shell480i,
    for (int i = 1; i < FRAMES; i++) cester_assert_uint_ne(s_bit31[i - 1], s_bit31[i]);
)

CESTER_TEST(clearsCoverBothFields, shell480i,
    cester_assert_uint_eq(239, count(0));
    cester_assert_uint_eq(239, count(1));
)
