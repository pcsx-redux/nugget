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

// mult/multu execution time as a function of the operands.
//
// Each measurement is 64 back-to-back "mult rs,rt; mflo" pairs, timed with
// root counter 2 on the system clock, run twice so the second pass comes
// out of the instruction cache. Every mflo stalls until its multiply is
// done, so the pair costs the multiply's latency plus a constant. The
// baseline is the same block with mult replaced by nop; the difference
// divided by 64 is the per-multiply stall, printed in hundredths.
//
// Arms:
//   rs sweep  - rs across the boundaries of the documented Fast/Med/Slow
//               classes, both signs, rt fixed.
//   rt sweep  - the same values in rt with a small rs, to show which
//               operand the early-out looks at.
//   busy      - a second mult or a div issued while a mult or div is still
//               running, and mtlo/mthi issued while a mult is running.
//               The two-instruction blocks subtract a nop; nop baseline.
//   mtlo/mthi - which value lo and hi hold after mult; mtlo; mflo, and
//               the same with enough nops for the mult to finish first.
//   div       - div and divu across operands, including rt=0.

#include <stdint.h>

#include "common/syscalls/syscalls.h"

#define T2_VALUE (*(volatile uint32_t *)0xbf801120)
#define T2_MODE (*(volatile uint32_t *)0xbf801124)

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}

static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

#define BLOCK(name, op)                                                     \
    static __attribute__((noinline)) uint32_t name(uint32_t rs, uint32_t rt) { \
        uint32_t t0, t1, lo;                                                \
        __asm__ volatile(                                                   \
            ".set push\n.set noreorder\n"                                   \
            "lw %0, 0(%4)\n"                                                \
            ".rept 64\n" op "\nmflo %2\n.endr\n"                            \
            "lw %1, 0(%4)\n"                                                \
            ".set pop\n"                                                    \
            : "=&r"(t0), "=&r"(t1), "=&r"(lo)                               \
            : "r"(rs), "r"(&T2_VALUE), "r"(rt)                              \
            : "hi", "lo", "memory");                                        \
        return (t1 - t0) & 0xffff;                                          \
    }

BLOCK(blockMult, "mult %3, %5")
BLOCK(blockMultu, "multu %3, %5")
BLOCK(blockBase, "nop")
BLOCK(blockBase2, "nop\nnop")
BLOCK(blockMultMult, "mult %3, %5\nmult %3, %5")
BLOCK(blockMultMultFast, "mult %3, %5\nmult $0, $0")
BLOCK(blockDivMultFast, "div $0, %3, %5\nmult $0, $0")
BLOCK(blockMultDiv, "mult %3, %5\ndiv $0, %3, %5")
BLOCK(blockMultMtlo, "mult %3, %5\nmtlo %3")
BLOCK(blockMultMthi, "mult %3, %5\nmthi %3")
BLOCK(blockNopMtlo, "nop\nmtlo %3")
BLOCK(blockDiv, "div $0, %3, %5")
BLOCK(blockDivu, "divu $0, %3, %5")

typedef uint32_t (*blockFn)(uint32_t, uint32_t);

static uint32_t measure(blockFn fn, uint32_t rs, uint32_t rt) {
    uint32_t sr = irqDisable();
    T2_MODE = 0;  // system clock, free running, resets the count
    fn(rs, rt);
    uint32_t t = fn(rs, rt);
    irqRestore(sr);
    return t;
}

static const uint32_t s_values[] = {
    0x00000000, 0x000007ff, 0x00000800, 0x000fffff, 0x00100000, 0x7fffffff,
    0xffffffff, 0xfffff801, 0xfffff800, 0xfffff7ff, 0xfff00001, 0xfff00000,
    0xffefffff, 0x80000000,
};
#define NVAL (sizeof(s_values) / sizeof(s_values[0]))

static uint32_t s_base;
static uint32_t s_base2;

static void report2(const char *arm, const char *op, uint32_t rs, uint32_t rt, uint32_t t, uint32_t base) {
    int32_t stall = (int32_t)(t - base) * 100 / 64;
    ramsyscall_printf("%s %-9s rs=%08x rt=%08x total=%d stall=%d.%02d\n", arm, op, rs, rt, (int)t, stall / 100,
                      stall % 100);
}

// mult rs, rt; mtlo k (or mthi k), then read lo and hi. With `wait` set,
// 32 nops sit between the mult and the mtlo so the mult has finished.
static void mtValues(uint32_t rs, uint32_t rt, uint32_t k) {
    uint32_t lo, hi, lo2, hi2, lo3, hi3;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "mult %4, %5\nmtlo %6\nmflo %0\nmfhi %1\n"
        "mult %4, %5\nmthi %6\nmflo %2\nmfhi %3\n"
        ".set pop\n"
        : "=&r"(lo), "=&r"(hi), "=&r"(lo2), "=&r"(hi2)
        : "r"(rs), "r"(rt), "r"(k)
        : "hi", "lo");
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "mult %2, %3\n.rept 32\nnop\n.endr\nmtlo %4\nmflo %0\nmfhi %1\n"
        ".set pop\n"
        : "=&r"(lo3), "=&r"(hi3)
        : "r"(rs), "r"(rt), "r"(k)
        : "hi", "lo");
    uint64_t p = (uint64_t)((int64_t)(int32_t)rs * (int32_t)rt);
    ramsyscall_printf("MTV rs=%08x rt=%08x k=%08x product=%08x:%08x | mtlo: lo=%08x hi=%08x | mthi: lo=%08x hi=%08x | mtlo after 32 nops: lo=%08x hi=%08x\n",
                      rs, rt, k, (uint32_t)(p >> 32), (uint32_t)p, lo, hi, lo2, hi2, lo3, hi3);
}

static const uint32_t s_divRs[] = {0x12345678, 0x7fffffff, 0x80000000, 0xffffffff, 0x00000001, 0x00000000};
static const uint32_t s_divRt[] = {0x00000003, 0x00000001, 0xffffffff, 0x7fffffff, 0x00010000, 0x00000000};

static void report(const char *arm, const char *op, uint32_t rs, uint32_t rt, uint32_t t) {
    int32_t stall = (int32_t)(t - s_base) * 100 / 64;
    ramsyscall_printf("%s %-5s rs=%08x rt=%08x total=%d stall=%d.%02d\n", arm, op, rs, rt, (int)t, stall / 100,
                      stall % 100);
}

int main(void) {
    ramsyscall_printf("MULT-TIMING-START\n");
    s_base = measure(blockBase, 0, 0);
    uint32_t base2 = measure(blockBase, 0, 0);
    ramsyscall_printf("BASE total=%d again=%d\n", (int)s_base, (int)base2);

    const uint32_t rtFixed = 0x12345678;
    const uint32_t rsSmall = 0x00000005;
    for (unsigned i = 0; i < NVAL; i++) {
        uint32_t v = s_values[i];
        report("RS", "mult", v, rtFixed, measure(blockMult, v, rtFixed));
        report("RS", "multu", v, rtFixed, measure(blockMultu, v, rtFixed));
    }
    for (unsigned i = 0; i < NVAL; i++) {
        uint32_t v = s_values[i];
        report("RT", "mult", rsSmall, v, measure(blockMult, rsSmall, v));
        report("RT", "multu", rsSmall, v, measure(blockMultu, rsSmall, v));
    }
    s_base2 = measure(blockBase2, 0, 0);
    ramsyscall_printf("BASE2 total=%d again=%d\n", (int)s_base2, (int)measure(blockBase2, 0, 0));
    const uint32_t slow = 0x12345678, fast = 0x00000005;
    report2("BUSY", "mult;mult", slow, rtFixed, measure(blockMultMult, slow, rtFixed), s_base2);
    report2("BUSY", "mult;mult", fast, rtFixed, measure(blockMultMult, fast, rtFixed), s_base2);
    report2("BUSY", "mult;multF", slow, rtFixed, measure(blockMultMultFast, slow, rtFixed), s_base2);
    report2("BUSY", "mult;multF", fast, rtFixed, measure(blockMultMultFast, fast, rtFixed), s_base2);
    report2("BUSY", "div;multF", slow, 3, measure(blockDivMultFast, slow, 3), s_base2);
    report2("BUSY", "mult;div", slow, 3, measure(blockMultDiv, slow, 3), s_base2);
    report2("BUSY", "mult;div", fast, 3, measure(blockMultDiv, fast, 3), s_base2);
    report2("BUSY", "mult;mtlo", slow, rtFixed, measure(blockMultMtlo, slow, rtFixed), s_base2);
    report2("BUSY", "mult;mthi", slow, rtFixed, measure(blockMultMthi, slow, rtFixed), s_base2);
    report2("BUSY", "nop;mtlo", slow, rtFixed, measure(blockNopMtlo, slow, rtFixed), s_base2);
    mtValues(slow, rtFixed, 0xa5a5a5a5);
    mtValues(fast, rtFixed, 0xa5a5a5a5);
    for (unsigned i = 0; i < sizeof(s_divRs) / sizeof(s_divRs[0]); i++) {
        for (unsigned j = 0; j < sizeof(s_divRt) / sizeof(s_divRt[0]); j++) {
            report2("DIV", "div", s_divRs[i], s_divRt[j], measure(blockDiv, s_divRs[i], s_divRt[j]), s_base);
            report2("DIV", "divu", s_divRs[i], s_divRt[j], measure(blockDivu, s_divRs[i], s_divRt[j]), s_base);
        }
    }
    ramsyscall_printf("MULT-TIMING-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
