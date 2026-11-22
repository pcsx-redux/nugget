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
 * GPUSTAT bits 31 and 13 around the 480i timeline that gpustat-480i checks:
 * other display ranges, PAL 480i, the interlace bit alone, and switching
 * interlace off and on. Readings from v2 GPUs (SCPH-5501, SCPH-7001,
 * SCPH-9002) and a v0 one (SCPH-1000), all 256 pixels wide, lines counted in
 * hblanks since the vblank IRQ:
 * - bit 31 rises on scanline Y1 of the frame following a 263-line one (313
 *   in PAL) and falls at the vblank; with Y1=10h and Y2=100h that is 23 lines
 *   after the IRQ, with Y2=F0h 39, with Y1=20h 39, in PAL with Y1=27h and
 *   Y2=125h 59. Outside 480i (progressive, or the interlace bit with 240
 *   lines) it toggles on every displayed line.
 * - bit 13 (v2 only) changes on the last scanline of each frame while the
 *   interlace bit is set: 5 and 6 lines after the IRQ with Y2=100h, 21 and 22
 *   with Y2=F0h, 18 and 19 in PAL. Turning interlace off freezes it, GP1(00h)
 *   sets it, and turning interlace on resumes the alternation from its
 *   frozen value: the field after the switch is a long one when it is 1.
 *   A v0 GPU leaves bit 13 clear in every mode recorded here.
 * Bit 13 asserts are skipped on a console
 * whose bit 13 does not change in the NTSC 480i baseline. PAL asserts are
 * skipped when no vblank arrives in PAL mode, as on one SCPH-1001.
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
#define NONE 0xffff
#define FIELDS 4
#define MODE_I 0x08000024
#define MODE_I2 0x08000020
#define MODE_P 0x08000000
#define MODE_PAL_I 0x0800002c
#define DISP_H 0x06c00200
#define DISP_H_PAL 0x06c80280
#define DISP_V(y1, y2) (0x07000000 | (y1) | ((y2) << 10))

struct Field {
    uint32_t firstStat;
    uint16_t lines;
    uint16_t b31Rise, b31Fall, b31First, b31Toggles, b13Rise, b13Fall;
};

static struct Field s_base[FIELDS], s_pal[FIELDS], s_y2f0[FIELDS], s_y1x20[FIELDS], s_i2[FIELDS], s_p[FIELDS];
static struct Field s_ipLong[FIELDS], s_ipShort[FIELDS], s_pi[FIELDS];
static uint32_t s_resetStat;
static uint16_t s_vbl;
static int s_interruptsWereEnabled;

static void setup(uint32_t h, uint32_t v, uint32_t mode) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = h;
    GPU_STATUS = v;
    GPU_STATUS = 0x05000000;
    GPU_STATUS = mode;
    GPU_STATUS = 0x03000000;
}

static uint16_t line(void) { return COUNTERS[1].value - s_vbl; }

// Waits for the vblank IRQ and restarts the line count there. 0 if none came
// within four NTSC fields, or within a few million polls when the hblank count
// does not move either (PAL mode on one SCPH-1001).
static int waitVBlank(void) {
    uint16_t start = COUNTERS[1].value;
    uint32_t polls = 0;
    while (!(IREG & 1)) {
        if ((uint16_t)(COUNTERS[1].value - start) > 1100) return 0;
        if (++polls > 4000000) return 0;
    }
    IREG = ~1u;
    s_vbl = COUNTERS[1].value;
    return 1;
}

static void waitLine(uint16_t l) { while (line() < l); }

// Records n fields from now; the first one starts at the current line.
static void record(struct Field *fields, int n) {
    for (int f = 0; f < n; f++) {
        struct Field *p = &fields[f];
        p->b31Rise = p->b31Fall = p->b31First = p->b13Rise = p->b13Fall = NONE;
        p->b31Toggles = 0;
        p->lines = NONE;
        uint32_t prev = GPU_STATUS;
        uint32_t polls = 0;
        p->firstStat = prev;
        while (1) {
            if (++polls > 4000000) return;
            uint32_t s = GPU_STATUS;
            uint16_t l = line();
            if (IREG & 1) {
                IREG = ~1u;
                p->lines = l;
                s_vbl = COUNTERS[1].value;
                break;
            }
            if (l > 1100) return;
            uint32_t rise = s & ~prev, fall = prev & ~s;
            if ((rise | fall) & 0x80000000) {
                p->b31Toggles++;
                if (p->b31First == NONE) p->b31First = l;
            }
            if ((rise & 0x80000000) && p->b31Rise == NONE) p->b31Rise = l;
            if ((fall & 0x80000000) && p->b31Fall == NONE) p->b31Fall = l;
            if ((rise & 0x2000) && p->b13Rise == NONE) p->b13Rise = l;
            if ((fall & 0x2000) && p->b13Fall == NONE) p->b13Fall = l;
            prev = s;
        }
    }
}

// Returns 0 when no vblank came in this mode at all. The IRQ flag is cleared
// after the mode is set, so a vblank left pending by the previous mode does
// not count.
static int recordMode(struct Field *fields, uint32_t h, uint32_t v, uint32_t mode) {
    for (int f = 0; f < FIELDS; f++) fields[f].lines = NONE;
    setup(h, v, mode);
    IREG = ~1u;
    if (!waitVBlank()) return 0;
    if (waitVBlank()) record(fields, FIELDS);
    return 1;
}

// In 480i, waits for a field whose bit 31 is (long) or is not (short) up by
// line 30, then writes the mode word there and records the rest of that
// field and FIELDS - 1 more.
static void switchAt30(struct Field *fields, int longField, uint32_t mode) {
    setup(DISP_H, DISP_V(0x10, 0x100), MODE_I);
    waitVBlank();
    for (int tries = 0; tries < 4; tries++) {
        if (!waitVBlank()) return;
        waitLine(30);
        if (!(GPU_STATUS & 0x80000000) == !longField) break;
    }
    GPU_STATUS = mode;
    record(fields, FIELDS);
}

static void print(const char *name, const struct Field *fields) {
    for (int f = 0; f < FIELDS; f++) {
        const struct Field *p = &fields[f];
        ramsyscall_printf("%s %d: lines=%d first=%08x b31 rise=%d fall=%d first=%d toggles=%d b13 rise=%d fall=%d\n",
                          name, f, p->lines, p->firstStat, p->b31Rise, p->b31Fall, p->b31First, p->b31Toggles,
                          p->b13Rise, p->b13Fall);
    }
}

static int isLong(const struct Field *p) { return p->b31Rise != NONE && p->b31Toggles == 1; }
static int bit13(const struct Field *p) { return (p->firstStat & 0x2000) != 0; }
static int isV2(void) {
    for (int f = 0; f < FIELDS; f++) {
        if (s_base[f].b13Rise != NONE || s_base[f].b13Fall != NONE) return 1;
    }
    return 0;
}
// PAL counts as run once a vblank came in PAL mode; a recording that stops
// after that fails checkInterlaced().
static int s_palRan;
static int palRan(void) { return s_palRan; }

// Interlaced fields alternate: in each pair, one has bit 31 rising at line
// b31 (within one line) with bit 13 falling at line b13Long, the other has no
// bit 31 rise and bit 13 rising at b13Long - 1.
static int checkInterlaced(const struct Field *fields, int n, uint16_t b31, uint16_t b13Long, int v2) {
    int bad = 0;
    for (int f = 0; f < n; f++) {
        const struct Field *p = &fields[f];
        if (p->lines == NONE) return 1;
        if (f && isLong(p) == isLong(&fields[f - 1])) bad++;
        if (isLong(p)) {
            if (p->b31Rise + 1 < b31 || p->b31Rise > b31 + 1) bad++;
            if (v2 && (!bit13(p) || p->b13Fall + 1 < b13Long || p->b13Fall > b13Long + 1)) bad++;
        } else {
            if (p->b31Toggles) bad++;
            if (v2 && (bit13(p) || p->b13Rise + 2 < b13Long || p->b13Rise > b13Long)) bad++;
        }
    }
    return bad;
}

// Progressive-style bit 31: one toggle per displayed line, the first one a
// line after Y1.
static int checkPerLine(const struct Field *p, uint16_t first) {
    if (p->lines == NONE) return 1;
    return p->b31Toggles < 235 || p->b31Toggles > 241 || p->b31First + 1 < first || p->b31First > first + 1;
}
)

CESTER_BEFORE_ALL(gpustatTimeline,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    COUNTERS[1].mode = 0x0100;
    recordMode(s_base, DISP_H, DISP_V(0x10, 0x100), MODE_I);
    s_palRan = recordMode(s_pal, DISP_H_PAL, DISP_V(0x27, 0x125), MODE_PAL_I);
    recordMode(s_y2f0, DISP_H, DISP_V(0x10, 0xf0), MODE_I);
    recordMode(s_y1x20, DISP_H, DISP_V(0x20, 0x100), MODE_I);
    recordMode(s_i2, DISP_H, DISP_V(0x10, 0x100), MODE_I2);
    recordMode(s_p, DISP_H, DISP_V(0x10, 0x100), MODE_P);
    switchAt30(s_ipShort, 0, MODE_P);
    switchAt30(s_ipLong, 1, MODE_P);
    // Bit 13 is frozen at 0 here; GP1(00h) sets it. A read straight after the write still
    // returns the old status on silicon, so it waits two lines.
    GPU_STATUS = 0x00000000;
    {
        uint16_t start = COUNTERS[1].value;
        while ((uint16_t)(COUNTERS[1].value - start) < 2);
    }
    s_resetStat = GPU_STATUS;
    // From 240p with bit 13 at 1, interlace on at line 30.
    setup(DISP_H, DISP_V(0x10, 0x100), MODE_P);
    waitVBlank();
    waitVBlank();
    waitLine(30);
    GPU_STATUS = MODE_I;
    record(s_pi, FIELDS);
    GPU_STATUS = 0x00000000;
    print("base", s_base);
    print("pal", s_pal);
    print("y2f0", s_y2f0);
    print("y1x20", s_y1x20);
    print("i2", s_i2);
    print("p", s_p);
    print("ip-short", s_ipShort);
    print("ip-long", s_ipLong);
    print("pi", s_pi);
    ramsyscall_printf("reset stat=%08x v2=%d pal=%d\n", s_resetStat, isV2(), palRan());
)

CESTER_AFTER_ALL(gpustatTimeline,
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(baseline, gpustatTimeline,
    cester_assert_int_eq(0, checkInterlaced(s_base, FIELDS, 23, 6, isV2()));
)

CESTER_TEST(palFields, gpustatTimeline,
    if (palRan()) cester_assert_int_eq(0, checkInterlaced(s_pal, FIELDS, 59, 19, isV2()));
)

CESTER_TEST(y2MovesBit13AndBit31, gpustatTimeline,
    cester_assert_int_eq(0, checkInterlaced(s_y2f0, FIELDS, 39, 22, isV2()));
)

CESTER_TEST(y1MovesBit31, gpustatTimeline,
    cester_assert_int_eq(0, checkInterlaced(s_y1x20, FIELDS, 39, 6, isV2()));
)

CESTER_TEST(bit31PerLineIn240p, gpustatTimeline,
    for (int f = 0; f < FIELDS; f++) cester_assert_int_eq(0, checkPerLine(&s_p[f], 24));
)

CESTER_TEST(interlaceBitAlone, gpustatTimeline,
    int v2 = isV2();
    for (int f = 0; f < FIELDS; f++) {
        const struct Field *p = &s_i2[f];
        // The first displayed line comes a line later after a 263-line frame.
        if (v2) {
            cester_assert_int_eq(0, checkPerLine(p, bit13(p) ? 24 : 23));
            if (f) cester_assert_int_ne(bit13(p), bit13(&s_i2[f - 1]));
        } else {
            cester_assert_int_eq(0, (checkPerLine(p, 24) && checkPerLine(p, 23)));
        }
    }
)

CESTER_TEST(bit13FrozenAfterInterlaceOff, gpustatTimeline,
    int v2 = isV2();
    for (int f = 1; f < FIELDS; f++) {
        cester_assert_int_eq(0, checkPerLine(&s_ipShort[f], 24));
        cester_assert_int_eq(0, checkPerLine(&s_ipLong[f], 24));
        cester_assert_uint_eq(NONE, s_ipShort[f].b13Rise);
        cester_assert_uint_eq(NONE, s_ipShort[f].b13Fall);
        cester_assert_uint_eq(NONE, s_ipLong[f].b13Rise);
        cester_assert_uint_eq(NONE, s_ipLong[f].b13Fall);
        if (v2) {
            cester_assert_int_eq(1, bit13(&s_ipShort[f]));
            cester_assert_int_eq(0, bit13(&s_ipLong[f]));
        }
    }
)

CESTER_TEST(bit13SetByReset, gpustatTimeline,
    if (isV2()) cester_assert_uint_ne(0, (s_resetStat & 0x2000));
)

CESTER_TEST(longFieldAfterInterlaceOn, gpustatTimeline,
    cester_assert_uint_eq(0, s_pi[0].b31Toggles);
    cester_assert_int_eq(0, checkInterlaced(&s_pi[1], FIELDS - 1, 23, 6, isV2()));
    cester_assert_int_eq(1, isLong(&s_pi[1]));
    if (isV2()) {
        cester_assert_int_eq(1, bit13(&s_pi[0]));
        cester_assert_uint_eq(NONE, s_pi[0].b13Fall);
    }
)
