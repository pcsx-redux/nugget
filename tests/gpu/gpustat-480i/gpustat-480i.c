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
 * GPUSTAT bits 31 and 13 in 640x480 interlaced NTSC, against the timeline
 * measured on a v2 GPU (SCPH-1001), in hblanks since the vblank that opened
 * each field: the fields alternate 263 and 262 lines; bit 31 is 1 from line 23
 * of the 263-line field until the next vblank and 0 everywhere else; bit 13 is
 * 1 from line 5 of the 262-line field until line 6 of the next 263-line field.
 *
 * The guest masks interrupts, counts hblanks on root counter 1 and polls
 * GPUSTAT and I_STAT bit 0 over FIELDS fields, recording per field the value
 * of both bits at the first sample after the vblank, the line each one first
 * read 1 and the line each one first read 0 after having read 1. Printing
 * waits until every field is recorded: a printf takes most of a field.
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
#define FIELDS 6
#define NONE 0xffff

struct Field {
    uint32_t firstStat;
    uint16_t lines;
    uint16_t b31Rise, b31Fall, b13Rise, b13Fall;
};

static struct Field s_fields[FIELDS];
static int s_interruptsWereEnabled;

static void record(void) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x06000000 | 0x260 | (0xc60 << 12);
    GPU_STATUS = 0x07000000 | 0x010 | (0x100 << 10);
    GPU_STATUS = 0x05000000;
    GPU_STATUS = 0x08000027;
    GPU_STATUS = 0x03000000;
    COUNTERS[1].mode = 0x0100;
    IREG = ~1u;
    while (!(IREG & 1));
    IREG = ~1u;
    uint16_t vbl = COUNTERS[1].value;
    for (int f = 0; f < FIELDS; f++) {
        struct Field *p = &s_fields[f];
        p->b31Rise = p->b31Fall = p->b13Rise = p->b13Fall = NONE;
        uint32_t prev = GPU_STATUS;
        p->firstStat = prev;
        while (1) {
            uint32_t s = GPU_STATUS;
            uint16_t line = COUNTERS[1].value - vbl;
            if (IREG & 1) {
                IREG = ~1u;
                p->lines = line;
                vbl = COUNTERS[1].value;
                break;
            }
            uint32_t rise = s & ~prev, fall = prev & ~s;
            if ((rise & 0x80000000) && p->b31Rise == NONE) p->b31Rise = line;
            if ((fall & 0x80000000) && p->b31Fall == NONE) p->b31Fall = line;
            if ((rise & 0x2000) && p->b13Rise == NONE) p->b13Rise = line;
            if ((fall & 0x2000) && p->b13Fall == NONE) p->b13Fall = line;
            prev = s;
        }
    }
    for (int f = 0; f < FIELDS; f++) {
        struct Field *p = &s_fields[f];
        ramsyscall_printf("field %d: lines=%d first=%08x b31 rise=%d fall=%d b13 rise=%d fall=%d\n", f, p->lines,
                          p->firstStat, p->b31Rise, p->b31Fall, p->b13Rise, p->b13Fall);
    }
}

static int isLong(const struct Field *p) { return p->b31Rise != NONE; }
)

CESTER_BEFORE_ALL(gpustat480i,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    record();
)

CESTER_AFTER_ALL(gpustat480i,
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(bit31ZeroAtVBlank, gpustat480i,
    for (int f = 0; f < FIELDS; f++) cester_assert_uint_eq(0, (s_fields[f].firstStat & 0x80000000));
)

CESTER_TEST(bit31AlternatesFields, gpustat480i,
    for (int f = 1; f < FIELDS; f++) cester_assert_int_ne(isLong(&s_fields[f - 1]), isLong(&s_fields[f]));
)

CESTER_TEST(bit31RisesAtLine23, gpustat480i,
    for (int f = 0; f < FIELDS; f++) {
        const struct Field *p = &s_fields[f];
        if (!isLong(p)) continue;
        cester_assert_uint_ge(p->b31Rise, 22);
        cester_assert_uint_le(p->b31Rise, 24);
        cester_assert_uint_eq(NONE, p->b31Fall);
    }
)

CESTER_TEST(bit13FollowsShortField, gpustat480i,
    for (int f = 1; f < FIELDS; f++) {
        const struct Field *p = &s_fields[f];
        if (isLong(p)) {
            cester_assert_uint_ne(0, (p->firstStat & 0x2000));
            cester_assert_uint_ge(p->b13Fall, 5);
            cester_assert_uint_le(p->b13Fall, 7);
            cester_assert_uint_eq(NONE, p->b13Rise);
        } else {
            cester_assert_uint_eq(0, (p->firstStat & 0x2000));
            cester_assert_uint_ge(p->b13Rise, 4);
            cester_assert_uint_le(p->b13Rise, 6);
            cester_assert_uint_eq(NONE, p->b13Fall);
        }
    }
)
