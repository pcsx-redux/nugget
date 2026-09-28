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
 * What BCC (FFFE0130h) bits 12..17 do, if anything.
 *
 * sys/memorycontrol.md names them INTP, RDPRI, NOPAD, BGNT, LDSCH and NOSTR
 * after the LR33300 manual, each "Supposedly". Flip one bit at a time away
 * from the value the BIOS leaves (0001E988h) and time a fixed set of
 * sequences against the unmodified value, IRQs masked, root counter 2 on the
 * system clock. A bit that moves no sequence is inert on this set, which is
 * a narrower statement than inert; the set is chosen to hit what the names
 * claim: bus reads and writes, the write buffer, load-use, scratchpad,
 * uncached instruction fetch, and a DMA transfer running against the CPU.
 * INTP is flipped with IRQs masked, so a change it makes to interrupt
 * delivery is outside this set by construction.
 *
 * Each figure is the minimum of 8 runs, and the whole row is taken twice
 * under every value; the two must agree or the row is UNSTABLE.
 */

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define BCC (*(volatile uint32_t *)0xfffe0130)
#define T2_VALUE (*(volatile uint32_t *)0xbf801120)
#define T2_MODE (*(volatile uint32_t *)0xbf801124)
#define BCC_DEFAULT 0x0001e988u

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}

static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

/* Written from uncached code, the way the BIOS does it. */
static __attribute__((noinline)) void setBccImpl(uint32_t v) {
    BCC = v;
    __asm__ volatile("nop; nop; nop; nop");
}
typedef void (*setBccFn)(uint32_t);
static void setBcc(uint32_t v) {
    setBccFn f = (setBccFn)(((uintptr_t)setBccImpl & 0x1fffffff) | 0xa0000000);
    f(v);
}

static volatile uint32_t s_ram[64] __attribute__((aligned(16)));

#define TIMED(body)                                                  \
    ({                                                               \
        uint32_t t0, t1;                                             \
        __asm__ volatile(".set push\n.set noreorder\n"               \
                         "lw %0, 0(%2)\n" body "lw %1, 0(%2)\n"      \
                         ".set pop\n"                                \
                         : "=&r"(t0), "=&r"(t1)                      \
                         : "r"(&T2_VALUE), "r"(ram), "r"(spad)       \
                         : "t0", "t1", "memory");                    \
        (t1 - t0) & 0xffff;                                          \
    })

static __attribute__((noinline)) uint32_t benchRamLoad(volatile uint32_t *ram, volatile uint32_t *spad) {
    return TIMED(".rept 32\nlw $t0, 0(%3)\naddu $t1, $t0, $t0\n.endr\n");
}
static __attribute__((noinline)) uint32_t benchRamStore(volatile uint32_t *ram, volatile uint32_t *spad) {
    return TIMED(".rept 32\nsw $t1, 0(%3)\n.endr\n");
}
static __attribute__((noinline)) uint32_t benchStoreLoad(volatile uint32_t *ram, volatile uint32_t *spad) {
    return TIMED(".rept 32\nsw $t1, 0(%3)\nlw $t0, 4(%3)\n.endr\n");
}
static __attribute__((noinline)) uint32_t benchUncachedLoad(volatile uint32_t *ram, volatile uint32_t *spad) {
    return TIMED(".rept 32\nlw $t0, 0(%3)\nnop\nnop\nnop\nnop\n.endr\n");
}
static __attribute__((noinline)) uint32_t benchSpad(volatile uint32_t *ram, volatile uint32_t *spad) {
    return TIMED(".rept 32\nlw $t0, 0(%4)\naddu $t1, $t0, $t0\nsw $t1, 4(%4)\n.endr\n");
}
static __attribute__((noinline)) uint32_t benchMmio(volatile uint32_t *ram, volatile uint32_t *spad) {
    return TIMED(".rept 32\nlw $t0, 0(%2)\n.endr\n");
}

typedef uint32_t (*benchFn)(volatile uint32_t *, volatile uint32_t *);

/* Uncached instruction fetch: the same block, called through KSEG1. */
static uint32_t runUncached(benchFn f, volatile uint32_t *ram, volatile uint32_t *spad) {
    benchFn u = (benchFn)(((uintptr_t)f & 0x1fffffff) | 0xa0000000);
    return u(ram, spad);
}

/* DMA6 (OT clear) of 4096 words to RAM while the CPU runs RAM loads, and
   separately how long that DMA takes to finish. The wait is bounded: a DMA
   that never completes reports 65535 instead of hanging the run. */
static uint32_t s_dmaDone;
static uint32_t benchDmaContend(volatile uint32_t *ram, volatile uint32_t *spad) {
    static uint32_t ot[4096];
    DPCR |= 0x08000000;
    DMA_CTRL[6].MADR = (uint32_t)&ot[4095];
    DMA_CTRL[6].BCR = 4096;
    uint32_t start = T2_VALUE;
    DMA_CTRL[6].CHCR = 0x11000002;
    uint32_t t = benchRamLoad(ram, spad);
    uint32_t n = 0;
    while ((DMA_CTRL[6].CHCR & 0x01000000) && n < 2000000) n++;
    s_dmaDone = (DMA_CTRL[6].CHCR & 0x01000000) ? 65535 : ((T2_VALUE - start) & 0xffff);
    return t;
}

struct Bench {
    const char *name;
    benchFn fn;
    int uncached;
};

static const struct Bench s_bench[] = {
    {"ramLoadUse", benchRamLoad, 0},   {"ramStore", benchRamStore, 0}, {"storeLoad", benchStoreLoad, 0},
    {"ramLoadNop", benchUncachedLoad, 0}, {"spad", benchSpad, 0},       {"mmio", benchMmio, 0},
    {"ifetchUnc", benchRamLoad, 1},    {"dmaContend", benchDmaContend, 0},
};
#define NBENCH (sizeof(s_bench) / sizeof(s_bench[0]))

static uint32_t measure(const struct Bench *b) {
    volatile uint32_t *ram = (volatile uint32_t *)(((uintptr_t)s_ram & 0x1fffffff) | 0xa0000000);
    volatile uint32_t *spad = (volatile uint32_t *)0x1f800100;
    T2_MODE = 0;
    /* Minimum of 8: DRAM refresh lands in some runs and not others. */
    uint32_t best = 0xffffffff;
    for (int i = 0; i < 8; i++) {
        uint32_t t = b->uncached ? runUncached(b->fn, ram, spad) : b->fn(ram, spad);
        if (t < best) best = t;
    }
    return best;
}

static uint32_t s_base[NBENCH];

static void row(const char *label, uint32_t bcc, int isBase) {
    uint32_t a[NBENCH], b[NBENCH], dmaA, dmaB;
    uint32_t sr = irqDisable();
    setBcc(bcc);
    for (unsigned i = 0; i < NBENCH; i++) a[i] = measure(&s_bench[i]);
    dmaA = s_dmaDone;
    for (unsigned i = 0; i < NBENCH; i++) b[i] = measure(&s_bench[i]);
    dmaB = s_dmaDone;
    uint32_t readBack = BCC;
    setBcc(BCC_DEFAULT);
    irqRestore(sr);

    int stable = 1, moved = 0;
    for (unsigned i = 0; i < NBENCH; i++) {
        if (a[i] != b[i]) stable = 0;
        if (isBase) s_base[i] = a[i];
        else if (a[i] != s_base[i]) moved = 1;
    }
    ramsyscall_printf("BCC %-6s set=%08x read=%08x", label, bcc, readBack);
    for (unsigned i = 0; i < NBENCH; i++) ramsyscall_printf(" %s=%d", s_bench[i].name, (int)a[i]);
    ramsyscall_printf(" dmaDone=%d/%d", (int)dmaA, (int)dmaB);
    ramsyscall_printf("%s%s\n", stable ? "" : " UNSTABLE", isBase ? " BASE" : moved ? " MOVED" : " same");
}

int main(void) {
    ramsyscall_printf("BCCBITS-START bcc=%08x\n", BCC);
    row("base", BCC_DEFAULT, 1);
    static const char *names[] = {"INTP", "RDPRI", "NOPAD", "BGNT", "LDSCH", "NOSTR"};
    for (int bit = 12; bit <= 17; bit++) row(names[bit - 12], BCC_DEFAULT ^ (1u << bit), 0);
    /* Positive control: IS1 off runs the cached rows from uncached fetch,
       so it must move them. */
    row("ctl-IS1", BCC_DEFAULT & ~(1u << 11), 0);
    row("base", BCC_DEFAULT, 0);
    ramsyscall_printf("BCCBITS-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
