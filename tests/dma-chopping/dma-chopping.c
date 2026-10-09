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
 * Does CHCR bit 8 (chopping) let the CPU run during a sync mode 0 transfer?
 *
 * DMA2, RAM -> GP0 only, 1024 words of GP0(00h), GP1(04h)=2.
 *
 * Per arm:
 *  LATCH: with the DPCR enable for DMA2 clear, CHCR is written with the arm
 *  value (busy set, so it waits), the I-loop runs 256 iterations reading it
 *  back (gives the busy readback and the alone I-loop rate), then CHCR is
 *  written without bit 24 and read back, then cleared.
 *  I-run: T2 is read, CHCR is written, then a loop of {read T2 into
 *  scratchpad, read CHCR} runs until CHCR busy clears. n = iterations, so
 *  n-1 = iterations completed while the channel was busy. T = cycles from
 *  before the CHCR store to the first T2 read after busy cleared. Every
 *  iteration touches the bus (T2 and CHCR are I/O reads): code running
 *  from the I-cache and scratchpad was measured to run through a DMA6 burst
 *  (dma-priority), so only a bus-touching loop can be stopped by one.
 *  A-run: T2, CHCR store, 300-iteration register-only loop, T2. Then wait.
 *  If the CPU kept running through the transfer, A is about max(alu, T);
 *  frozen, about T + alu.
 */

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define T2_VALUE (*(volatile uint32_t *)0xbf801120)
#define T2_MODE (*(volatile uint32_t *)0xbf801124)
#define CHCR2 (*(volatile uint32_t *)0xbf8010a8)
#define SPAD ((volatile uint32_t *)0x1f800000)
#define BUSY 0x01000000u
#define NWORDS 1024
#define LIMIT 200000u
#define ALU_N 300
#define REPS 4

static uint32_t s_src[NWORDS] __attribute__((aligned(16)));

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}

static void delay(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++);
}

struct IRes {
    uint32_t t0, tend, n, last;
};

/* Stores timestamps to scratchpad words 0..255 (the last slot repeats). */
static __attribute__((noinline)) void iloop(uint32_t chcr, uint32_t limit, struct IRes *r) {
    uint32_t t0, tend, n, c;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t9, 0x1f80\n"
        "move  $t8, $zero\n"
        "move  %2, $zero\n"
        "lui   $t6, 0x0100\n"
        "lw    %0, 0(%5)\n"
        "sw    %4, 0(%6)\n"
        "1:\n"
        "lw    $t0, 0(%5)\n"
        "addiu %2, %2, 1\n"
        "addu  $t1, $t9, $t8\n"
        "sw    $t0, 0($t1)\n"
        "sltiu $t2, $t8, 0x3fc\n"
        "sll   $t2, $t2, 2\n"
        "addu  $t8, $t8, $t2\n"
        "lw    %3, 0(%6)\n"
        "nop\n"
        "and   $t2, %3, $t6\n"
        "beqz  $t2, 2f\n"
        "nop\n"
        "bne   %2, %7, 1b\n"
        "nop\n"
        "2:\n"
        "lw    %1, 0(%5)\n"
        "nop\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(tend), "=&r"(n), "=&r"(c)
        : "r"(chcr), "r"(&T2_VALUE), "r"(&CHCR2), "r"(limit)
        : "t0", "t1", "t2", "t6", "t8", "t9", "memory");
    r->t0 = t0;
    r->tend = tend;
    r->n = n;
    r->last = c;
}

static __attribute__((noinline)) uint32_t aloop(uint32_t chcr) {
    uint32_t t0, t1, k = ALU_N;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lw    %0, 0(%4)\n"
        "sw    %3, 0(%5)\n"
        "1:\n"
        "addiu %2, %2, -1\n"
        "bnez  %2, 1b\n"
        "nop\n"
        "lw    %1, 0(%4)\n"
        "nop\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t1), "+r"(k)
        : "r"(chcr), "r"(&T2_VALUE), "r"(&CHCR2)
        : "memory");
    return (t1 - t0) & 0xffff;
}

static int waitIdle(void) {
    uint32_t n = 0;
    while ((CHCR2 & BUSY) && n < LIMIT) n++;
    if (CHCR2 & BUSY) {
        CHCR2 = 0;
        delay(1000);
        return 0;
    }
    return 1;
}

static void gpuPrep(void) {
    GPU_STATUS = 0x00000000u; /* GP1(00h) reset */
    delay(2000);
    GPU_STATUS = 0x04000002u; /* DMA direction CPU -> GP0 */
}

static void setup(uint32_t bcr) {
    gpuPrep();
    DPCR |= 0x00000800u;
    DMA_CTRL[2].MADR = (uint32_t)s_src;
    DMA_CTRL[2].BCR = bcr;
}

/* Gap statistics over the scratchpad timestamps. */
static void gaps(uint32_t t0, uint32_t n, uint32_t base) {
    uint32_t m = n < 256 ? n : 256;
    uint32_t prev = t0, big = 0, maxg = 0, sumBig = 0;
    for (uint32_t i = 0; i < m; i++) {
        uint32_t g = (SPAD[i] - prev) & 0xffff;
        prev = SPAD[i];
        if (i == 0) continue;
        if (g > maxg) maxg = g;
        if (g > base + 8) big++, sumBig += g;
    }
    ramsyscall_printf(" first=%u maxgap=%u biggaps=%u sumbig=%u g:", (SPAD[0] - t0) & 0xffff, maxg, big, sumBig);
    prev = SPAD[0];
    for (uint32_t i = 1; i < m && i < 17; i++) {
        ramsyscall_printf(" %u", (SPAD[i] - prev) & 0xffff);
        prev = SPAD[i];
    }
}

struct Arm {
    const char *name;
    uint32_t chcr, bcr;
};

static uint32_t s_aluAlone, s_iAlone;

static void runArm(const struct Arm *a) {
    struct IRes r;
    /* LATCH, enable clear. */
    gpuPrep();
    DPCR &= ~0x00000800u;
    DMA_CTRL[2].MADR = (uint32_t)s_src;
    DMA_CTRL[2].BCR = a->bcr;
    iloop(a->chcr, 256, &r);
    uint32_t busyRb = r.last;
    uint32_t iAlone = ((SPAD[255] - SPAD[0]) & 0xffff) / 255;
    CHCR2 = a->chcr & ~BUSY;
    uint32_t idleRb = CHCR2;
    CHCR2 = 0;
    uint32_t zeroRb = CHCR2;
    ramsyscall_printf("LATCH %-9s wrote=%08x busyRb=%08x idleRb=%08x zeroRb=%08x ialone=%u.%02u\n", a->name, a->chcr,
                      busyRb, idleRb, zeroRb, ((SPAD[255] - SPAD[0]) & 0xffff) / 255,
                      (((SPAD[255] - SPAD[0]) & 0xffff) % 255) * 100 / 255);
    s_iAlone = iAlone;

    for (int rep = 0; rep < REPS; rep++) {
        setup(a->bcr);
        ramsyscall_printf("RUN %-9s rep=%d I ...", a->name, rep);
        iloop(a->chcr, LIMIT, &r);
        int ok = !(r.last & BUSY);
        if (!ok) ok = waitIdle() ? 2 : 0;
        uint32_t endChcr = CHCR2, endMadr = DMA_CTRL[2].MADR, endBcr = DMA_CTRL[2].BCR, gs = GPU_STATUS;
        ramsyscall_printf(" %s n=%u T=%u endCHCR=%08x MADR=%08x BCR=%08x gpustat=%08x", ok == 1 ? "done" : "TIMEOUT",
                          r.n, (r.tend - r.t0) & 0xffff, endChcr, endMadr, endBcr, gs);
        gaps(r.t0, r.n, iAlone);
        ramsyscall_printf("\n");

        setup(a->bcr);
        uint32_t al = aloop(a->chcr);
        int ok2 = waitIdle();
        ramsyscall_printf("RUN %-9s rep=%d A %s alu=%u alone=%u\n", a->name, rep, ok2 ? "done" : "TIMEOUT", al,
                          s_aluAlone);
    }
    DPCR &= ~0x00000800u;
}

#define CHOP(dw, cw) (0x01000101u | ((dw) << 16) | ((cw) << 20))

static const struct Arm s_arms[] = {
    {"burst", 0x01000001u, NWORDS},
    {"slice16", 0x01000201u, ((NWORDS / 16) << 16) | 16},
    {"chop2/2", CHOP(2, 2), NWORDS},
    {"chop2/4", CHOP(2, 4), NWORDS},
    {"chop2/6", CHOP(2, 6), NWORDS},
    {"chop4/2", CHOP(4, 2), NWORDS},
    {"chop4/4", CHOP(4, 4), NWORDS},
    {"chop4/6", CHOP(4, 6), NWORDS},
    {"burst-end", 0x01000001u, NWORDS},
};

int main(void) {
    uint32_t sr = irqDisable();
    uint32_t dpcr0 = DPCR;
    T2_MODE = 0;
    for (int i = 0; i < NWORDS; i++) s_src[i] = 0x00000000u; /* GP0(00h) NOP */
    ramsyscall_printf("DMACHOP-START dpcr=%08x dicr=%08x words=%d alu_n=%d\n", DPCR, DICR, NWORDS, ALU_N);
    DPCR &= ~0x00000800u;
    uint32_t a = 0xffff;
    for (int i = 0; i < 4; i++) {
        uint32_t v = aloop(0);
        if (v < a) a = v;
    }
    s_aluAlone = a;
    ramsyscall_printf("ALONE alu=%u\n", a);
    for (unsigned i = 0; i < sizeof(s_arms) / sizeof(s_arms[0]); i++) runArm(&s_arms[i]);
    GPU_STATUS = 0x00000000u;
    DPCR = dpcr0;
    ramsyscall_printf("OBS done\n");
    (void)sr;
    return 0;
}
