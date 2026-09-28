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
    ramsyscall_printf("MULT-TIMING-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
