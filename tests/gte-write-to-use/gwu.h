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
 * Varied: N = 0..3; A = 0..3, the word within a 16-byte cache line the
 * sequence starts at (A0..A3, cached); and cached vs uncached fetch (U,
 * at A = 0). Held: interrupts off, nop as the filler and as the
 * instruction before the second write, mtc2/ctc2 as the writer, the
 * measured pass is the second of two back to back.
 *
 * The last column of each cell is the cached result from SCPH-1000, -1001,
 * -5501 (two) -7001 and -9002 (see RESULTS.md). A letter that is the same
 * at every A is checked against it, and the program returns 1 on any
 * difference; '?' marks a letter that moves with A or between consoles,
 * which is printed and not checked. The uncached pass is printed only.
 */

#pragma once

#include <stdint.h>

#include "gte-latency-common.h"

#define GWU_NMAX 4
#define GWU_AMAX 4

typedef void (*GwuFn)(uint32_t, uint32_t);

typedef struct {
    const char* label;
    void (*scene)(void);
    uint32_t (*read)(void);
    GwuFn fn[GWU_NMAX][GWU_AMAX];
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

// A = how many words past a 16-byte cache line boundary the sequence starts.
#define GWU_FN(W, name, op, reg, N, A)                                                                   \
    static void name##_##N##_##A(uint32_t a, uint32_t b) {                                               \
        __asm__ volatile(".set push\n.set noreorder\n.balign 16\n.rept " #A "\nnop\n.endr\n" W " %0, $" #reg \
                         "\nnop\nnop\nnop\nnop\n" W " %1, $" #reg "\n.rept " #N                            \
                         "\nnop\n.endr\ncop2 %2\nnop\nnop\n.set pop\n"                                     \
                         :                                                                               \
                         : "r"(a), "r"(b), "i"(op));                                                     \
    }

#define GWU_FN_A(W, name, op, reg, N) \
    GWU_FN(W, name, op, reg, N, 0)    \
    GWU_FN(W, name, op, reg, N, 1)    \
    GWU_FN(W, name, op, reg, N, 2)    \
    GWU_FN(W, name, op, reg, N, 3)

#define GWU_FUNCS(K, name, scene, op, reg, canary, label, expect) \
    GWU_FN_A(GWU_W_##K, name, op, reg, 0)                 \
    GWU_FN_A(GWU_W_##K, name, op, reg, 1)                 \
    GWU_FN_A(GWU_W_##K, name, op, reg, 2)                 \
    GWU_FN_A(GWU_W_##K, name, op, reg, 3)                 \
    static uint32_t name##_rd(void) {                     \
        uint32_t v;                                       \
        GWU_GET_##K(reg, v);                              \
        return v;                                         \
    }

#define GWU_ROW(name, N) {name##_##N##_0, name##_##N##_1, name##_##N##_2, name##_##N##_3}
#define GWU_ENTRY(K, name, scene, op, reg, canary, label, expect)                                         \
    {label, scene, name##_rd, {GWU_ROW(name, 0), GWU_ROW(name, 1), GWU_ROW(name, 2), GWU_ROW(name, 3)}, \
     canary, GWU_ISC_##K, reg, expect},
