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


/*
 * DPCR priority fields, and the tie-break sys/dmachannels.md describes as
 * "DMA0=Lowest, DMA6=Highest, CPU=higher than DMA6?".
 *
 * Part 1, CPU against a transfer: DMA6 (OT clear, 4096 words, burst, and
 * chopped) or DMA2 (VRAM read, 1024 words, sync mode 1) is started, then the
 * CPU reads root counter 2 and runs a work loop: uncached RAM loads,
 * scratchpad loads, or a cached ALU loop. stall is the time to that first
 * counter read, loop the work loop, done the whole transfer. DPCR bits 28..30
 * (CPU) and the channel's priority field are varied.
 *
 * Part 2, DMA2 against DMA6: both channels are programmed and started with
 * their master enables clear, then enabled by one DPCR write. DMA2 reads a
 * filled VRAM rectangle into a RAM buffer, ascending; DMA6 writes the OT
 * clear pattern into the same buffer, descending. The last writer of each
 * word shows the order, and the index where the patterns change shows where
 * one channel cut into the other.
 *
 * Part 3, the start of a burst: DMA6 is started and the first counter read
 * follows after 0..12 nops. Each of the 8 samples prints stall/loop/done; the
 * first sample runs from a cold instruction cache.
 *
 * Part 1 and Part 2 rows run twice and are marked UNSTABLE when the runs
 * differ: by more than 3 cycles in Part 1, at all in Part 2.
 */

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define T2_VALUE (*(volatile uint32_t *)0xbf801120)
#define T2_MODE (*(volatile uint32_t *)0xbf801124)
#define BUSY 0x01000000u
#define DPCR_BASE 0x07654321u

static uint32_t s_dpcr0;

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}
static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

/* Priority fields only; enable bits are added by the caller. */
static uint32_t prio(unsigned ch2, unsigned ch6, unsigned cpu) {
    return (DPCR_BASE & ~0x77000700u & ~0x88888888u) | (ch2 << 8) | (ch6 << 24) | (cpu << 28);
}

static int waitIdle(unsigned ch) {
    uint32_t n = 0;
    while ((DMA_CTRL[ch].CHCR & BUSY) && n < 2000000) n++;
    return !(DMA_CTRL[ch].CHCR & BUSY);
}

/* ---- Part 2 ---- */

#define NWORDS 1024
#define FILLWORD 0x001f001fu
#define POISON 0xdeadbeefu
static uint32_t s_buf[NWORDS] __attribute__((aligned(16)));

static void gpuCmd(uint32_t c) {
    while (!(GPU_STATUS & 0x04000000));
    GPU_DATA = c;
}

static void fillVram(void) {
    GPU_STATUS = 0x04000000; /* GP1(04h)=0, no DMA request */
    gpuCmd(0x020000f8);       /* fill, r=F8h -> 001Fh */
    GPU_DATA = 0x00000000;    /* x=0, y=0 */
    GPU_DATA = 0x00200040;    /* 64x32 */
    while (!(GPU_STATUS & 0x04000000));
}

/* Leave the VRAM read (64x32 = 1024 words) waiting in GPUREAD. */
static void armVramRead(void) {
    gpuCmd(0x01000000); /* clear cache */
    gpuCmd(0xc0000000);
    GPU_DATA = 0x00000000;
    GPU_DATA = 0x00200040;
    uint32_t n = 0;
    while (!(GPU_STATUS & 0x08000000) && n < 100000) n++;
    GPU_STATUS = 0x04000003; /* DMA request = GPUREAD */
}

struct DmaResult {
    int held2, held6, touchedWhileHeld;
    int ok2, ok6;
    int otc, gpu, poison, other, changes, firstGpu, firstOtc;
};

static int isOtc(unsigned i, uint32_t v) {
    if (i == 0) return v == 0x00ffffffu;
    return v == (((uint32_t)&s_buf[i - 1]) & 0x00ffffffu);
}

static void dmaSample(uint32_t prios, int use2, int use6, struct DmaResult *r) {
    for (unsigned i = 0; i < NWORDS; i++) s_buf[i] = POISON;
    fillVram();
    if (use2) armVramRead();
    DPCR = prios; /* both enables clear */
    if (use2) {
        DMA_CTRL[2].MADR = (uint32_t)s_buf;
        DMA_CTRL[2].BCR = ((NWORDS / 16) << 16) | 16;
        DMA_CTRL[2].CHCR = 0x01000200;
    }
    if (use6) {
        DMA_CTRL[6].MADR = (uint32_t)&s_buf[NWORDS - 1];
        DMA_CTRL[6].BCR = NWORDS;
        DMA_CTRL[6].CHCR = 0x11000002;
    }
    for (volatile int d = 0; d < 20000; d++);
    r->held2 = use2 ? !!(DMA_CTRL[2].CHCR & BUSY) : -1;
    r->held6 = use6 ? !!(DMA_CTRL[6].CHCR & BUSY) : -1;
    r->touchedWhileHeld = 0;
    for (unsigned i = 0; i < NWORDS; i++)
        if (s_buf[i] != POISON) r->touchedWhileHeld++;
    DPCR = prios | (use2 ? 0x00000800u : 0) | (use6 ? 0x08000000u : 0);
    r->ok2 = use2 ? waitIdle(2) : -1;
    r->ok6 = use6 ? waitIdle(6) : -1;
    GPU_STATUS = 0x04000000;
    r->otc = r->gpu = r->poison = r->other = r->changes = 0;
    r->firstGpu = r->firstOtc = -1;
    int prev = -1;
    for (unsigned i = 0; i < NWORDS; i++) {
        uint32_t v = s_buf[i];
        int k;
        if (isOtc(i, v)) {
            k = 0;
            r->otc++;
            if (r->firstOtc < 0) r->firstOtc = i;
        } else if (v == FILLWORD) {
            k = 1;
            r->gpu++;
            if (r->firstGpu < 0) r->firstGpu = i;
        } else if (v == POISON) {
            k = 2;
            r->poison++;
        } else {
            k = 3;
            r->other++;
        }
        if (prev >= 0 && k != prev) r->changes++;
        prev = k;
    }
}

static void dmaRow(const char *label, unsigned p2, unsigned p6, int use2, int use6) {
    struct DmaResult a, b;
    uint32_t prios = prio(p2, p6, 0);
    uint32_t sr = irqDisable();
    dmaSample(prios, use2, use6, &a);
    dmaSample(prios, use2, use6, &b);
    DPCR = s_dpcr0;
    irqRestore(sr);
    int stable = a.otc == b.otc && a.gpu == b.gpu && a.poison == b.poison && a.firstGpu == b.firstGpu &&
                 a.firstOtc == b.firstOtc && a.changes == b.changes && a.touchedWhileHeld == b.touchedWhileHeld;
    ramsyscall_printf(
        "DMA %-8s dma2=%d dma6=%d held2=%d held6=%d touchedWhileHeld=%d done2=%d done6=%d otc=%d gpu=%d "
        "poison=%d other=%d changes=%d firstGpu=%d firstOtc=%d%s\n",
        label, p2, p6, a.held2, a.held6, a.touchedWhileHeld, a.ok2, a.ok6, a.otc, a.gpu, a.poison, a.other,
        a.changes, a.firstGpu, a.firstOtc, stable ? "" : " UNSTABLE");
}

/* ---- Part 1 ---- */

static uint32_t s_ot[4096];
static volatile uint32_t s_word __attribute__((aligned(16)));

static __attribute__((noinline)) uint32_t loads(volatile uint32_t *p) {
    uint32_t t0, t1;
    __asm__ volatile(".set push\n.set noreorder\n"
                     "lw %0, 0(%2)\n"
                     ".rept 256\nlw $t0, 0(%3)\n.endr\n"
                     "lw %1, 0(%2)\n"
                     ".set pop\n"
                     : "=&r"(t0), "=&r"(t1)
                     : "r"(&T2_VALUE), "r"(p)
                     : "t0", "memory");
    return (t1 - t0) & 0xffff;
}

static __attribute__((noinline)) uint32_t spadLoads(volatile uint32_t *p) {
    (void)p;
    return loads((volatile uint32_t *)0x1f800100);
}

/* No bus access between the two counter reads except the second read. */
static __attribute__((noinline)) uint32_t aluSpin(volatile uint32_t *p) {
    uint32_t t0, t1, n = 600;
    (void)p;
    __asm__ volatile(".set push\n.set noreorder\n"
                     "lw %0, 0(%3)\n"
                     "1: addiu %2, %2, -1\n"
                     "bnez %2, 1b\n"
                     "nop\n"
                     "lw %1, 0(%3)\n"
                     ".set pop\n"
                     : "=&r"(t0), "=&r"(t1), "+r"(n)
                     : "r"(&T2_VALUE)
                     : "memory");
    return (t1 - t0) & 0xffff;
}

typedef uint32_t (*workFn)(volatile uint32_t *);
enum { M_BURST6, M_BURST6_ALU, M_BURST6_SPAD, M_CHOP6, M_SYNC1_2, M_COUNT, M_SYNC0_2 };
static const char *s_modeName[] = {"burst6", "burst6-alu", "burst6-spad", "chop6", "sync1-2"};
static const workFn s_work[] = {loads, aluSpin, spadLoads, loads, loads};

struct CpuRow {
    uint32_t alone, stall, loop, done;
    int stuck;
};

static void cpuSample(int mode, uint32_t dpcr, struct CpuRow *r) {
    volatile uint32_t *p = (volatile uint32_t *)(((uintptr_t)&s_word & 0x1fffffff) | 0xa0000000);
    int gpu = mode == M_SYNC0_2 || mode == M_SYNC1_2;
    unsigned ch = gpu ? 2 : 6;
    workFn work = s_work[mode];
    r->alone = r->stall = r->loop = r->done = 0xffffffff;
    r->stuck = 0;
    if (gpu) fillVram();
    for (int i = 0; i < 8; i++) {
        uint32_t a = work(p);
        if (a < r->alone) r->alone = a;
        DPCR = dpcr | (ch == 2 ? 0x00000800u : 0x08000000u);
        uint32_t chcr;
        if (gpu) {
            armVramRead();
            DMA_CTRL[2].MADR = (uint32_t)s_buf;
            if (mode == M_SYNC1_2) {
                DMA_CTRL[2].BCR = ((NWORDS / 16) << 16) | 16;
                chcr = 0x01000200;
            } else {
                DMA_CTRL[2].BCR = NWORDS;
                chcr = 0x11000000;
            }
        } else {
            DMA_CTRL[6].MADR = (uint32_t)&s_ot[4095];
            DMA_CTRL[6].BCR = 4096;
            /* chopping: 2^4 words of DMA, then 2^4 clocks of CPU */
            chcr = mode == M_CHOP6 ? 0x11440102 : 0x11000002;
        }
        uint32_t start = T2_VALUE;
        DMA_CTRL[ch].CHCR = chcr;
        uint32_t after = T2_VALUE;
        uint32_t l = work(p);
        int ok = waitIdle(ch);
        uint32_t end = T2_VALUE;
        GPU_STATUS = 0x04000000;
        if (!ok) {
            r->stuck = 1;
            continue;
        }
        uint32_t st = (after - start) & 0xffff, d = (end - start) & 0xffff;
        if (st < r->stall) r->stall = st;
        if (l < r->loop) r->loop = l;
        if (d < r->done) r->done = d;
    }
}

static int near(uint32_t x, uint32_t y) { return (x > y ? x - y : y - x) <= 3; }

static void cpuRow(int mode, const char *label, unsigned cpu, unsigned chp) {
    struct CpuRow a, b;
    uint32_t dpcr = (mode == M_SYNC0_2 || mode == M_SYNC1_2) ? prio(chp, 7, cpu) : prio(3, chp, cpu);
    uint32_t sr = irqDisable();
    T2_MODE = 0;
    cpuSample(mode, dpcr, &a);
    cpuSample(mode, dpcr, &b);
    uint32_t readBack = DPCR;
    DPCR = s_dpcr0;
    irqRestore(sr);
    int stable = near(a.alone, b.alone) && near(a.stall, b.stall) && near(a.loop, b.loop) && near(a.done, b.done);
    ramsyscall_printf("CPU %-11s %-6s cpu=%d dma=%d dpcr=%08x alone=%d stall=%d loop=%d done=%d%s%s\n",
                      s_modeName[mode], label, cpu, chp, readBack, (int)a.alone, (int)a.stall, (int)a.loop,
                      (int)a.done, (a.stuck || b.stuck) ? " STUCK" : "", stable ? "" : " UNSTABLE");
}

/* ---- Part 3: the first bus read after starting a burst ---- */

/* Start DMA6 with the CHCR store, run N nops, then a counter read and 256
   uncached RAM loads. out[0] = cycles from before the store to the read after
   the nops, out[1] = the load loop. */
#define DEF_RACE(N)                                                                  \
    static __attribute__((noinline)) void race##N(volatile uint32_t *chcr, uint32_t v, \
                                                  volatile uint32_t *p, uint32_t *out) { \
        uint32_t t0, t1, t2;                                                         \
        __asm__ volatile(".set push\n.set noreorder\n"                               \
                         "lw %0, 0(%3)\n"                                            \
                         "sw %5, 0(%4)\n"                                            \
                         ".rept " #N "\nnop\n.endr\n"                                \
                         "lw %1, 0(%3)\n"                                            \
                         ".rept 256\nlw $t8, 0(%6)\n.endr\n"                         \
                         "lw %2, 0(%3)\n"                                            \
                         ".set pop\n"                                                \
                         : "=&r"(t0), "=&r"(t1), "=&r"(t2)                           \
                         : "r"(&T2_VALUE), "r"(chcr), "r"(v), "r"(p)                 \
                         : "t8", "memory");                                          \
        out[0] = (t1 - t0) & 0xffff;                                                 \
        out[1] = (t2 - t1) & 0xffff;                                                 \
        out[2] = t0;                                                                 \
    }
DEF_RACE(0)
DEF_RACE(1)
DEF_RACE(2)
DEF_RACE(3)
DEF_RACE(4)
DEF_RACE(6)
DEF_RACE(8)
DEF_RACE(12)
typedef void (*raceFn)(volatile uint32_t *, uint32_t, volatile uint32_t *, uint32_t *);
static const raceFn s_race[] = {race0, race1, race2, race3, race4, race6, race8, race12};
static const int s_raceN[] = {0, 1, 2, 3, 4, 6, 8, 12};

/* Every sample is printed: the point is whether the outcome is bimodal. */
static void raceRow(unsigned k, unsigned cpu, unsigned ch6) {
    volatile uint32_t *p = (volatile uint32_t *)(((uintptr_t)&s_word & 0x1fffffff) | 0xa0000000);
    uint32_t stall[8], loop[8], done[8];
    uint32_t sr = irqDisable();
    T2_MODE = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t out[3];
        DPCR = prio(3, ch6, cpu) | 0x08000000u;
        DMA_CTRL[6].MADR = (uint32_t)&s_ot[4095];
        DMA_CTRL[6].BCR = 4096;
        s_race[k](&DMA_CTRL[6].CHCR, 0x11000002, p, out);
        int ok = waitIdle(6);
        uint32_t end = T2_VALUE;
        stall[i] = out[0];
        loop[i] = out[1];
        done[i] = ok ? ((end - out[2]) & 0xffff) : 65535;
    }
    DPCR = s_dpcr0;
    irqRestore(sr);
    ramsyscall_printf("RACE nops=%-2d cpu=%d dma6=%d", s_raceN[k], cpu, ch6);
    for (int i = 0; i < 8; i++) ramsyscall_printf(" %d/%d/%d", (int)stall[i], (int)loop[i], (int)done[i]);
    ramsyscall_printf("\n");
}

int main(void) {
    s_dpcr0 = DPCR;
    ramsyscall_printf("DMAPRIO-START dpcr=%08x\n", DPCR);
    for (int m = 0; m < M_COUNT; m++) {
        cpuRow(m, "cpuHi", 0, 7);
        cpuRow(m, "dmaHi", 7, 0);
        cpuRow(m, "tie3", 3, 3);
    }

    for (unsigned k = 0; k < 8; k++) raceRow(k, 0, 7);
    for (unsigned k = 0; k < 8; k++) raceRow(k, 7, 0);

    /* Controls: each channel alone through the same hold-then-enable path. */
    dmaRow("alone2", 3, 7, 1, 0);
    dmaRow("alone6", 3, 7, 0, 1);
    dmaRow("default", 3, 7, 1, 1);
    dmaRow("dma2hi", 0, 7, 1, 1);
    dmaRow("dma6hi", 7, 0, 1, 1);
    dmaRow("tie3", 3, 3, 1, 1);
    dmaRow("tie0", 0, 0, 1, 1);
    dmaRow("alone2", 3, 7, 1, 0);
    ramsyscall_printf("DMAPRIO-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
