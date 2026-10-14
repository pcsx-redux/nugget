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
 * Write-to-use delay, per cop2 command and per input register.
 *
 * Every cell is one (command, input register) pair of the gte-latency tests,
 * with that test's scene and canary. A cell function writes the register's
 * scene value, waits 4 nops, writes a second value, waits N nops, runs the
 * command:
 *
 *   mtc2/ctc2 a, reg; nop x4; mtc2/ctc2 b, reg; nop x N; cop2 CMD
 *
 * and the command's outputs are compared against two references run through
 * the same function: a = b = scene value (old), a = b = canary (new).
 * The written register itself is left out of the comparison.
 *
 * Varied: N = 0..3, and whether the cell code is fetched cached (C) or
 * uncached (U). Held: interrupts off, nop as the filler and as the
 * instruction before the second write, mtc2/ctc2 as the writer, the
 * measured pass is the second of two back to back.
 *
 * The last column of each cell is what SCPH-1000, -1001, -5501 and -7001
 * printed for N = 0..3 cached (see RESULTS.md). The cached passes are
 * checked against it and the program returns 1 on any difference; a '?'
 * is a letter that moved between builds or between consoles, and is not
 * checked. The uncached pass is printed, not checked.
 */

#pragma once

#include <stdint.h>

#include "gte-latency-common.h"

#define GWU_NMAX 4

typedef void (*GwuFn)(uint32_t, uint32_t);

typedef struct {
    const char* label;
    void (*scene)(void);
    uint32_t (*read)(void);
    GwuFn fn[GWU_NMAX];
    uint32_t canary;
    uint8_t ctrl;
    uint8_t reg;
    const char* expect;
} GwuCell;

#define GWU_W_D "mtc2"
#define GWU_W_C "ctc2"
#define GWU_GET_D cop2_get
#define GWU_GET_C cop2_getc
#define GWU_ISC_D 0
#define GWU_ISC_C 1

#define GWU_FN(W, name, op, reg, N)                                                                      \
    static void name##_##N(uint32_t a, uint32_t b) {                                                     \
        __asm__ volatile(".set push\n.set noreorder\n" W " %0, $" #reg "\nnop\nnop\nnop\nnop\n" W        \
                         " %1, $" #reg "\n.rept " #N "\nnop\n.endr\ncop2 %2\nnop\nnop\n.set pop\n"         \
                         :                                                                               \
                         : "r"(a), "r"(b), "i"(op));                                                     \
    }

#define GWU_FUNCS(K, name, scene, op, reg, canary, label, expect) \
    GWU_FN(GWU_W_##K, name, op, reg, 0)                   \
    GWU_FN(GWU_W_##K, name, op, reg, 1)                   \
    GWU_FN(GWU_W_##K, name, op, reg, 2)                   \
    GWU_FN(GWU_W_##K, name, op, reg, 3)                   \
    static uint32_t name##_rd(void) {                     \
        uint32_t v;                                       \
        GWU_GET_##K(reg, v);                              \
        return v;                                         \
    }

#define GWU_ENTRY(K, name, scene, op, reg, canary, label, expect) \
    {label, scene, name##_rd, {name##_0, name##_1, name##_2, name##_3}, canary, GWU_ISC_##K, reg, expect},
