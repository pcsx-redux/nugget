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
 * Does the SPU drop 32-bit writes? spu/soundprocessingunitspu.md says it
 * "occasionally seems to miss" them. A sw to an SPU register pair is two
 * SBUS halfword writes; this checks, many thousands of times, that both land.
 *
 * Targets are halfword pairs that read back what was written and have no
 * side effect while nothing plays: reverb configuration (1F801DC0h..) with
 * reverb off, and voice 0 ADSR (1F801C08h/0Ah) with voice 0 keyed off.
 *
 * Arms, per target:
 *   sw     one sw, then both halves read back at once
 *   sh     two sh, same readback (control for the 32-bit path)
 *   swLate one sw, readback after about 2000 cycles
 *   burst  sw to 8 consecutive pairs back to back, then all read back
 *   sb1    sb to byte 1, which the SPU is known to drop: must count every
 *          iteration as a miss, or the detector is broken
 * A miss is recorded per half, with the OR of the wrong bits, and a first
 * read that is wrong is re-read after a delay to tell late from lost.
 */

#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#define N 8192

static uint32_t s_x = 0x12345678;
static uint32_t rnd(void) {
    s_x ^= s_x << 13;
    s_x ^= s_x >> 17;
    s_x ^= s_x << 5;
    return s_x;
}

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}
static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

static void delay(void) {
    for (volatile int i = 0; i < 300; i++);
}

struct Stats {
    uint32_t n, missLo, missHi, lateLo, lateHi, badBitsLo, badBitsHi;
    uint32_t exWrote, exGot;
};

static void check(struct Stats *s, volatile uint16_t *r, uint32_t v) {
    uint16_t lo = r[0], hi = r[1];
    uint16_t wl = v, wh = v >> 16;
    s->n++;
    if (lo != wl || hi != wh) {
        if (!s->exWrote && !s->exGot) {
            s->exWrote = v;
            s->exGot = lo | ((uint32_t)hi << 16);
        }
        delay();
        uint16_t lo2 = r[0], hi2 = r[1];
        if (lo != wl) {
            if (lo2 == wl) s->lateLo++;
            else {
                s->missLo++;
                s->badBitsLo |= lo2 ^ wl;
            }
        }
        if (hi != wh) {
            if (hi2 == wh) s->lateHi++;
            else {
                s->missHi++;
                s->badBitsHi |= hi2 ^ wh;
            }
        }
    }
}

static void print(const char *target, const char *arm, struct Stats *s) {
    ramsyscall_printf("SPUW %-6s %-6s n=%d missLo=%d missHi=%d lateLo=%d lateHi=%d badBits=%04x/%04x", target, arm,
                      (int)s->n, (int)s->missLo, (int)s->missHi, (int)s->lateLo, (int)s->lateHi, (int)s->badBitsLo,
                      (int)s->badBitsHi);
    if (s->exWrote || s->exGot) ramsyscall_printf(" first wrote=%08x got=%08x", s->exWrote, s->exGot);
    ramsyscall_printf("\n");
}

static void run(const char *name, volatile uint16_t *r, uint32_t mask) {
    struct Stats s;
    volatile uint32_t *w = (volatile uint32_t *)r;
    uint32_t sr = irqDisable();

    __builtin_memset(&s, 0, sizeof(s));
    for (int i = 0; i < N; i++) {
        uint32_t v = rnd() & mask;
        *w = v;
        check(&s, r, v);
    }
    print(name, "sw", &s);

    __builtin_memset(&s, 0, sizeof(s));
    for (int i = 0; i < N; i++) {
        uint32_t v = rnd() & mask;
        r[0] = v;
        r[1] = v >> 16;
        check(&s, r, v);
    }
    print(name, "sh", &s);

    __builtin_memset(&s, 0, sizeof(s));
    for (int i = 0; i < N / 4; i++) {
        uint32_t v = rnd() & mask;
        *w = v;
        delay();
        check(&s, r, v);
    }
    print(name, "swLate", &s);

    /* Known-dropped byte write: byte 1 of the low half. Every iteration
       starts from a value that differs from the target in the low half. */
    __builtin_memset(&s, 0, sizeof(s));
    for (int i = 0; i < N / 8; i++) {
        uint32_t v = rnd() & mask;
        *w = v ^ 0x0000ff00u;
        ((volatile uint8_t *)r)[1] = v >> 8;
        check(&s, r, v);
    }
    print(name, "sb1", &s);

    irqRestore(sr);
}

/* Per slot of the burst: misses, and how many of them read back the
   slot's value from before the burst (the write never landed). */
static void burst(const char *label) {
    volatile uint16_t *r = (volatile uint16_t *)0xbf801dc0;
    volatile uint32_t *w = (volatile uint32_t *)r;
    struct Stats s;
    uint32_t v[8];
    uint16_t prevLo[8];
    uint32_t slotMiss[8] = {0}, slotPrev[8] = {0};
    uint32_t sr = irqDisable();
    __builtin_memset(&s, 0, sizeof(s));
    for (int i = 0; i < N / 8; i++) {
        for (int k = 0; k < 8; k++) {
            v[k] = rnd();
            prevLo[k] = r[2 * k];
        }
        w[0] = v[0];
        w[1] = v[1];
        w[2] = v[2];
        w[3] = v[3];
        w[4] = v[4];
        w[5] = v[5];
        w[6] = v[6];
        w[7] = v[7];
        for (int k = 0; k < 8; k++) {
            uint16_t lo = r[2 * k];
            if (lo != (uint16_t)v[k]) {
                slotMiss[k]++;
                if (lo == prevLo[k]) slotPrev[k]++;
            }
            check(&s, r + 2 * k, v[k]);
        }
    }
    irqRestore(sr);
    print(label, "burst", &s);
    ramsyscall_printf("SPUW %-6s slots", label);
    for (int k = 0; k < 8; k++) ramsyscall_printf(" %d:%d/%d", k, (int)slotMiss[k], (int)slotPrev[k]);
    ramsyscall_printf("\n");
}

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)

int main(void) {
    uint16_t ctrl = SPU_CTRL;
    ramsyscall_printf("SPUW-START spucnt=%04x spudelay=%08x\n", ctrl, *(volatile uint32_t *)0xbf801014);
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_REVERB_EN_LOW = 0;
    SPU_REVERB_EN_HIGH = 0;
    muteSpu();
    SPU_CTRL = ctrl & ~0x0080; /* reverb master off */

    for (int pass = 0; pass < 2; pass++) {
        run("rev0", (volatile uint16_t *)0xbf801dc0, 0xffffffffu);
        run("adsr0", (volatile uint16_t *)0xbf801c08, 0xffffffffu);
        burst("rev0-7");
    }

    /* The same under other SPU_DELAY (1F801014h) settings. */
    static const uint32_t delays[] = {0x200931e1, 0x200931ef, 0x200931ff, 0x200930e1, 0x200933e1, 0x200935e1};
    uint32_t d0 = SPU_DELAY;
    for (unsigned i = 0; i < sizeof(delays) / sizeof(delays[0]); i++) {
        SPU_DELAY = delays[i];
        ramsyscall_printf("SPUW-DELAY set=%08x read=%08x\n", delays[i], SPU_DELAY);
        run("rev0", (volatile uint16_t *)0xbf801dc0, 0xffffffffu);
        burst("rev0-7");
        SPU_DELAY = d0;
    }
    ramsyscall_printf("SPUW-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
