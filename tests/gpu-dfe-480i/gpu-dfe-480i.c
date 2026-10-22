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
 * GP0(E1h).10 (drawing to the display area) in 640x480 interlaced NTSC, with
 * the display area at (0,0), against what a v0 (SCPH-1000) and a v2
 * (SCPH-1001) GPU were both measured to do: with the bit set every row is
 * drawn; with it clear, a row whose parity equals GPUSTAT.31 at the time of
 * drawing is skipped. GPUSTAT.31 reads 1 from line 23 of the 263-line field
 * until the next vblank and 0 everywhere else, so in that stretch only even
 * rows are drawn and elsewhere only odd ones. GP0(02h) fills follow the same
 * rule.
 *
 * Each arm clears 640x480 with the bit set, sets the arm's value, draws 480
 * 640x1 rectangles from y=0 down (or one fill), and reads the column x=0..1
 * back. "Fast" arms start at line 30 of the 263-line field and send rows back
 * to back; the "slow" arm sends one row per hblank from the vblank that opens
 * the 263-line field, so row y is drawn on about line y and the list ends in
 * the 262-line field.
 */

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
#define ROWS 480

static uint8_t s_rect1[ROWS];
static uint8_t s_rect0[ROWS];
static uint8_t s_slow0[ROWS];
static uint8_t s_fill0[ROWS];
static uint32_t s_col[ROWS];
static unsigned s_frame, s_lastLong;
static uint16_t s_vbl;
static int s_interruptsWereEnabled;

static void waitIdle(void) {
    while ((GPU_STATUS & 0x14000000) != 0x14000000);
}

static void poll(void) {
    if (IREG & 1) {
        IREG = ~1u;
        s_frame++;
        s_vbl = COUNTERS[1].value;
    }
    if (GPU_STATUS & 0x80000000) s_lastLong = s_frame;
}

static uint16_t line(void) { return COUNTERS[1].value - s_vbl; }

static void setup(void) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x06000000 | 0x260 | (0xc60 << 12);
    GPU_STATUS = 0x07000000 | 0x010 | (0x100 << 10);
    GPU_STATUS = 0x05000000;
    GPU_STATUS = 0x08000027;
    GPU_DATA = 0xe3000000;
    GPU_DATA = 0xe4000000 | 639 | (479 << 10);
    GPU_DATA = 0xe5000000;
    GPU_STATUS = 0x03000000;
    COUNTERS[1].mode = 0x0100;
}

static void fill(uint32_t color) {
    GPU_DATA = 0x02000000 | color;
    GPU_DATA = 0x00000000;
    GPU_DATA = (ROWS << 16) | 640;
}

static void readColumn(uint8_t *out, int black) {
    waitIdle();
    GPU_DATA = 0x01000000;
    GPU_DATA = 0xc0000000;
    GPU_DATA = 0x00000000;
    GPU_DATA = (ROWS << 16) | 2;
    while (!(GPU_STATUS & 0x08000000));
    for (int i = 0; i < ROWS; i++) s_col[i] = GPU_DATA;
    for (int i = 0; i < ROWS; i++) out[i] = ((s_col[i] & 0x7fff7fff) == 0) == black;
}

// Waits for the vblank that opens a 263-line field, then for the given line.
static void syncLong(uint16_t at) {
    IREG = ~1u;
    s_frame = 0;
    s_lastLong = 0xffff;
    while (s_frame < 3) poll();
    while (s_frame - s_lastLong != 1) poll();
    unsigned f = s_frame;
    while (s_frame == f) poll();
    while (line() < at) poll();
}

static void rects(uint8_t *out, int dfe, int slow) {
    setup();
    GPU_DATA = 0xe1000400;
    fill(0);
    waitIdle();
    GPU_DATA = 0xe1000000 | (dfe ? 0x400 : 0);
    syncLong(slow ? 0 : 30);
    for (int y = 0; y < ROWS; y++) {
        if (slow) {
            uint16_t l = COUNTERS[1].value;
            while (COUNTERS[1].value == l) poll();
        }
        GPU_DATA = 0x60ffffff;
        GPU_DATA = y << 16;
        GPU_DATA = (1 << 16) | 640;
        while ((GPU_STATUS & 0x14000000) != 0x14000000) poll();
    }
    readColumn(out, 0);
}

static void fillArm(uint8_t *out) {
    setup();
    GPU_DATA = 0xe1000400;
    fill(0xffffff);
    waitIdle();
    GPU_DATA = 0xe1000000;
    syncLong(30);
    fill(0);
    readColumn(out, 1);
}

static void print(const char *tag, const uint8_t *rows) {
    char buf[61];
    for (int base = 0; base < ROWS; base += 60) {
        for (int i = 0; i < 60; i++) buf[i] = rows[base + i] ? '#' : '.';
        buf[60] = 0;
        ramsyscall_printf("%s %03d %s\n", tag, base, buf);
    }
}

static unsigned countRange(const uint8_t *rows, int from, int to, int parity) {
    unsigned n = 0;
    for (int y = from; y <= to; y++)
        if (((y & 1) == parity) && rows[y]) n++;
    return n;
}
)

CESTER_BEFORE_ALL(gpudfe480i,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    rects(s_rect1, 1, 0);
    rects(s_rect0, 0, 0);
    rects(s_slow0, 0, 1);
    fillArm(s_fill0);
    print("rect1", s_rect1);
    print("rect0", s_rect0);
    print("slow0", s_slow0);
    print("fill0", s_fill0);
)

CESTER_AFTER_ALL(gpudfe480i,
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(dfeSetDrawsEveryRow, gpudfe480i,
    cester_assert_uint_eq(240, countRange(s_rect1, 0, ROWS - 1, 0));
    cester_assert_uint_eq(240, countRange(s_rect1, 0, ROWS - 1, 1));
)

CESTER_TEST(dfeClearSkipsOddRowsWhileBit31Set, gpudfe480i,
    cester_assert_uint_eq(240, countRange(s_rect0, 0, ROWS - 1, 0));
    cester_assert_uint_eq(0, countRange(s_rect0, 0, ROWS - 1, 1));
)

CESTER_TEST(dfeClearFollowsTheBeam, gpudfe480i,
    // Rows 0-19: before bit 31 rises, odd rows only.
    cester_assert_uint_eq(0, countRange(s_slow0, 0, 19, 0));
    cester_assert_uint_eq(10, countRange(s_slow0, 0, 19, 1));
    // Rows 30-249: bit 31 set, even rows only.
    cester_assert_uint_eq(110, countRange(s_slow0, 30, 249, 0));
    cester_assert_uint_eq(0, countRange(s_slow0, 30, 249, 1));
    // Rows 280-479: the 262-line field, odd rows only.
    cester_assert_uint_eq(0, countRange(s_slow0, 280, 479, 0));
    cester_assert_uint_eq(100, countRange(s_slow0, 280, 479, 1));
)

CESTER_TEST(fillFollowsDfe, gpudfe480i,
    cester_assert_uint_eq(240, countRange(s_fill0, 0, ROWS - 1, 0));
    cester_assert_uint_eq(0, countRange(s_fill0, 0, ROWS - 1, 1));
)
