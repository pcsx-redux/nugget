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

// Channel registers written while a sync-1 transfer waits between blocks.
//
//   c_* arms: DMA2, 4 blocks of 8 into a 16x4 VRAM rectangle, block 0 forced
//     with GP1(04h)=0, then CHCR written with bit 24 kept (c_none writes
//     nothing, c_same the same value, c_step sets bit 1, c_dir clears bit 0,
//     c_dirstep does both), then GP1(04h)=2. 8 CPU filler words complete the
//     rectangle before the readback.
//   s_* arms: DMA4, 4 blocks of 4 into SPU RAM, block 0 forced with the SPUCNT
//     transfer mode at stop, then MADR and/or BCR written, then mode 2.
//
// On an SCPH-1000, 5501 and 7001: CHCR reads back as written and bits 0 and 1
// apply from the next block. Bit 1 makes the remaining blocks step down from
// the current MADR; clearing bit 0 makes them write RAM with what GPUREAD
// returns. DMA4 follows a rewritten MADR and BCR like DMA2 does. Redux runs a
// transfer to completion when it starts, so those arms only run on hardware.
// p8 and s_plain are the controls.

#include <stdint.h>

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/hardware/dma.h"
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

#define GP0 (*(volatile uint32_t *)0x1f801810)
#define GP1 (*(volatile uint32_t *)0x1f801814)
#define LIMIT 400000
#define RX 64
#define RY 64
#define GX 48
#define GY 60
#define GW 48
#define GH 16
#define SENT 0x0e0eu
#define SENTW ((SENT << 16) | SENT)

static uint32_t bufA[64] __attribute__((aligned(4)));
static uint32_t rb[GW * GH / 2];
static int s_rw, s_rh;

static uint32_t xy(int x, int y) { return ((uint32_t)(uint16_t)y << 16) | (uint16_t)x; }

static void gpuSetup(void) {
    GP1 = 0x00000000;
    GP1 = 0x01000000;
    GP1 = 0x03000000;
    GP1 = 0x08000001;
    GP1 = 0x06c60260;
    GP1 = 0x07040010;
    GP0 = 0xe1000400;
    GP0 = 0xe3000000;
    GP0 = 0xe4000000 | (511 << 10) | 1023;
    GP0 = 0xe5000000;
    GP0 = 0xe6000000;
}

static int waitIdle(void) {
    int i = 0;
    while (i < LIMIT && (GP1 & 0x14000000) != 0x14000000) i++;
    return i < LIMIT;
}

static void fillPatterns(void) {
    for (int i = 0; i < 64; i++) bufA[i] = ((uint32_t)(0xa000 + 2 * i + 1) << 16) | (0xa000 + 2 * i);
}

// Filler word k: top byte 0, so past the rectangle it is a GP0(00h).
static uint32_t filler(int k) { return ((uint32_t)(k & 0xff) << 16) | (0xc000 | k); }

static void cpuFeed(int n) {
    GP1 = 0x04000001;
    for (int k = 0; k < n; k++) {
        int p = 0;
        while (p < LIMIT && !(GP1 & 0x02000000)) p++;
        GP0 = filler(k);
    }
}

static void fillGuard(void) {
    GP1 = 0x04000001;
    GP0 = 0xa0000000;
    GP0 = xy(GX, GY);
    GP0 = xy(GW, GH);
    for (int i = 0; i < GW * GH / 2; i++) {
        int p = 0;
        while (p < LIMIT && !(GP1 & 0x02000000)) p++;
        GP0 = SENTW;
    }
    waitIdle();
    GP1 = 0x04000000;
}

static int s_readOk;
static void readGuard(void) {
    GP1 = 0x00000000;
    GP1 = 0x01000000;
    GP1 = 0x04000000;
    waitIdle();
    GP0 = 0xc0000000;
    GP0 = xy(GX, GY);
    GP0 = xy(GW, GH);
    int i = 0;
    while (i < LIMIT && !(GP1 & 0x08000000)) i++;
    s_readOk = i < LIMIT;
    for (int w = 0; w < GW * GH / 2; w++) rb[w] = GP0;
}

static uint16_t px(int x, int y) {
    int idx = (y - GY) * GW + (x - GX);
    uint32_t w = rb[idx >> 1];
    return (idx & 1) ? (w >> 16) : (w & 0xffff);
}

static uint32_t rectWord(int k) {
    int p = 2 * k;
    int x = RX + p % s_rw, y = RY + p / s_rw;
    return ((uint32_t)px(x + 1, y) << 16) | px(x, y);
}

static int s_guardBad;
static void checkGuard(void) {
    s_guardBad = 0;
    for (int y = GY; y < GY + GH; y++)
        for (int x = GX; x < GX + GW; x++) {
            int in = x >= RX && x < RX + s_rw && y >= RY && y < RY + s_rh;
            if (!in && px(x, y) != SENT) s_guardBad++;
        }
}

// Walk the rectangle: DMA words (source index), then filler, then sentinel.
// s_dmaN = DMA words before anything else, s_ordered = they are 0..n-1 in
// order, s_fillAt = position of the first filler word (-1 none),
// s_fillN = filler words, s_sentN = sentinel words, s_junk = anything else.
static int s_dmaN, s_ordered, s_fillAt, s_fillN, s_sentN, s_junk;
static void walk(const char *name) {
    int words = s_rw * s_rh / 2;
    s_dmaN = 0;
    s_ordered = 1;
    s_fillAt = -1;
    s_fillN = s_sentN = s_junk = 0;
    ramsyscall_printf("IDX %s", name);
    for (int k = 0; k < words; k++) {
        uint32_t w = rectWord(k);
        uint32_t lo = w & 0xffff, hi = w >> 16;
        if (w == SENTW) {
            s_sentN++;
        } else if ((lo & 0xf000) == 0xa000 && hi == lo + 1) {
            int idx = (lo - 0xa000) / 2;
            ramsyscall_printf(" %d", idx);
            if (s_fillN == 0 && s_sentN == 0) {
                if (idx != s_dmaN) s_ordered = 0;
                s_dmaN++;
            } else {
                s_junk++;
            }
        } else if ((lo & 0xf000) == 0xc000 && hi == ((lo & 0xfff) & 0xff)) {
            if (s_fillAt < 0) s_fillAt = k;
            ramsyscall_printf(" f%d", lo & 0xfff);
            s_fillN++;
        } else {
            ramsyscall_printf(" ?%08x", w);
            s_junk++;
        }
    }
    ramsyscall_printf("\n");
    checkGuard();
    ramsyscall_printf("RESULT %s dma %d ordered %d fillAt %d fill %d sent %d junk %d guardBad %d readOk %d\n", name,
                      s_dmaN, s_ordered, s_fillAt, s_fillN, s_sentN, s_junk, s_guardBad, s_readOk);
}

static int dmaBusy(void) { return (DMA_CTRL[DMA_GPU].CHCR & 0x01000000) != 0; }

static int s_done, s_stepOk;
static void start(int rw, int rh) {
    s_rw = rw;
    s_rh = rh;
    fillPatterns();
    gpuSetup();
    fillGuard();
    GP1 = 0x01000000;
    GP1 = 0x04000000;
    waitIdle();
    GP0 = 0xa0000000;
    GP0 = xy(RX, RY);
    GP0 = xy(rw, rh);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)bufA;
}

static void waitDone(void) {
    int i = 0;
    while (i < LIMIT && dmaBusy()) i++;
    s_done = i < LIMIT;
    if (!s_done) DMA_CTRL[DMA_GPU].CHCR = 0;
}

static void endArm(const char *name) {
    ramsyscall_printf("OBS %s end %s MADR %08x BCR %08x CHCR %08x GPUSTAT %08x bufA %08x\n", name,
                      s_done ? "done" : "stuck", DMA_CTRL[DMA_GPU].MADR, DMA_CTRL[DMA_GPU].BCR,
                      DMA_CTRL[DMA_GPU].CHCR, GP1, (uint32_t)(uintptr_t)bufA & 0xffffff);
}

// One block of n words, forced (paced == 0) or DREQ-paced, then 64 filler.
static void oneBlock(int n, int paced, const char *name) {
    ramsyscall_printf("STEP %s begin\n", name);
    start(16, 8);
    DMA_CTRL[DMA_GPU].BCR = (1 << 16) | n;
    s_stepOk = 1;
    if (paced) {
        GP1 = 0x04000002;
        DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    } else {
        int w = 0;
        while (w < LIMIT && !(GP1 & 0x10000000)) w++;
        s_stepOk = w < LIMIT;
        DMA_CTRL[DMA_GPU].CHCR = 0x11000201;
    }
    waitDone();
    for (int i = 0; i < 2000; i++) __asm__ volatile("");
    endArm(name);
    cpuFeed(64);
    waitIdle();
    readGuard();
    walk(name);
    GP1 = 0x01000000;
    GP1 = 0;
}

// mode 0: nothing, 1: flush 4, 2: flush 8, 3: long wait, 4: flush 16.
// Blocks of bs words, 32 words in all, into the 16x4 rectangle.
static void tailN(int forced, int bs, int mode, const char *name) {
    ramsyscall_printf("STEP %s begin\n", name);
    start(16, 4);
    s_stepOk = 1;
    if (forced) {
        DMA_CTRL[DMA_GPU].BCR = ((32 / bs) << 16) | bs;
        int w = 0;
        while (w < LIMIT && !(GP1 & 0x10000000)) w++;
        DMA_CTRL[DMA_GPU].CHCR = 0x11000201;
        int i = 0;
        while (i < LIMIT && (DMA_CTRL[DMA_GPU].CHCR & 0x10000000)) i++;
        s_stepOk = w < LIMIT && i < LIMIT;
        for (i = 0; i < 2000; i++) __asm__ volatile("");
        ramsyscall_printf("OBS %s paused MADR %08x BCR %08x CHCR %08x\n", name, DMA_CTRL[DMA_GPU].MADR,
                          DMA_CTRL[DMA_GPU].BCR, DMA_CTRL[DMA_GPU].CHCR);
        GP1 = 0x04000002;
    } else {
        DMA_CTRL[DMA_GPU].BCR = (3 << 16) | 8;
        GP1 = 0x04000002;
        DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    }
    waitDone();
    for (int i = 0; i < 2000; i++) __asm__ volatile("");
    endArm(name);
    ramsyscall_printf("OBS %s pre gpustat %08x\n", name, GP1);
    if (mode == 1) cpuFeed(4);
    if (mode == 2) cpuFeed(8);
    if (mode == 4) cpuFeed(16);
    if (mode == 3) for (int i = 0; i < 2000000; i++) __asm__ volatile("");
    ramsyscall_printf("OBS %s post gpustat %08x\n", name, GP1);
    readGuard();
    walk(name);
    GP1 = 0x01000000;
    GP1 = 0;
}

// One paced block of k words into a w x h rect, nothing after it.
static void shortRect(int w, int h, int k, const char *name) {
    ramsyscall_printf("STEP %s begin\n", name);
    start(w, h);
    DMA_CTRL[DMA_GPU].BCR = (1 << 16) | k;
    GP1 = 0x04000002;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    waitDone();
    for (int i = 0; i < 2000; i++) __asm__ volatile("");
    endArm(name);
    readGuard();
    walk(name);
    GP1 = 0x01000000;
    GP1 = 0;
}


// CHCR rewritten (bit 24 kept) while DMA2 waits between blocks. 4 blocks of 8
// into a 16x4 rect, block 0 forced with GP1(04h)=0, then the CHCR write, then
// GP1(04h)=2. 8 filler words before the readback so no partial row is held.
static uint32_t s_cw, s_cr, s_cEnd, s_mEnd, s_bEnd;
static int s_bufChanged;
static void chcrArm(uint32_t newChcr, const char *name) {
    ramsyscall_printf("STEP %s begin\n", name);
    start(16, 4);
    DMA_CTRL[DMA_GPU].BCR = (4 << 16) | 8;
    int w = 0;
    while (w < LIMIT && !(GP1 & 0x10000000)) w++;
    DMA_CTRL[DMA_GPU].CHCR = 0x11000201;
    int i = 0;
    while (i < LIMIT && (DMA_CTRL[DMA_GPU].CHCR & 0x10000000)) i++;
    s_stepOk = w < LIMIT && i < LIMIT;
    for (i = 0; i < 2000; i++) __asm__ volatile("");
    uint32_t m1 = DMA_CTRL[DMA_GPU].MADR, c1 = DMA_CTRL[DMA_GPU].CHCR;
    s_cw = newChcr;
    if (newChcr) DMA_CTRL[DMA_GPU].CHCR = newChcr;
    s_cr = DMA_CTRL[DMA_GPU].CHCR;
    GP1 = 0x04000002;
    waitDone();
    for (i = 0; i < 2000; i++) __asm__ volatile("");
    s_cEnd = DMA_CTRL[DMA_GPU].CHCR;
    s_mEnd = DMA_CTRL[DMA_GPU].MADR;
    s_bEnd = DMA_CTRL[DMA_GPU].BCR;
    s_bufChanged = 0;
    for (i = 0; i < 64; i++)
        if (bufA[i] != (((uint32_t)(0xa000 + 2 * i + 1) << 16) | (0xa000 + 2 * i))) s_bufChanged++;
    ramsyscall_printf("OBS %s paused MADR %08x CHCR %08x wrote %08x read %08x end %s MADR %08x BCR %08x CHCR %08x bufA %08x bufChanged %d\n",
                      name, m1, c1, s_cw, s_cr, s_done ? "done" : "stuck", s_mEnd, s_bEnd, s_cEnd,
                      (uint32_t)(uintptr_t)bufA & 0xffffff, s_bufChanged);
    if (s_bufChanged) {
        ramsyscall_printf("BUF %s", name);
        for (i = 0; i < 64; i++) ramsyscall_printf(" %08x", bufA[i]);
        ramsyscall_printf("\n");
    }
    cpuFeed(8);
    waitIdle();
    readGuard();
    walk(name);
    GP1 = 0x01000000;
    GP1 = 0;
}

// DMA4 (SPU) MADR/BCR rewritten while the channel waits between blocks.
#define SPUCNT (*(volatile uint16_t *)0x1f801daa)
#define SPUSTAT (*(volatile uint16_t *)0x1f801dae)
#define SPU_ADDR (*(volatile uint16_t *)0x1f801da6)
#define SPU_TCTRL (*(volatile uint16_t *)0x1f801dac)
#define SPU_BASE 0x10000u
static uint32_t s_sA[16] __attribute__((aligned(4)));
static uint32_t s_sB[16] __attribute__((aligned(4)));
static uint32_t s_sFill[32] __attribute__((aligned(4)));
static uint32_t s_sOut[32] __attribute__((aligned(4)));
static int spuMode(unsigned mode) {
    SPUCNT = (SPUCNT & ~0x0030u) | (mode << 4);
    int n = 0;
    while ((SPUSTAT & 0x0030u) != (mode << 4) && n < 100000) n++;
    return n < 100000;
}
static void spuWait(void) {
    int n = 0;
    while ((SPUSTAT & 0x0400u) && n < 100000) n++;
}
static int spuDma(uint32_t madr, uint32_t bcr, uint32_t chcr) {
    DMA_CTRL[DMA_SPU].MADR = madr;
    DMA_CTRL[DMA_SPU].BCR = bcr;
    DMA_CTRL[DMA_SPU].CHCR = chcr;
    int n = 0;
    while (n < LIMIT && (DMA_CTRL[DMA_SPU].CHCR & 0x01000000)) n++;
    if (n >= LIMIT) DMA_CTRL[DMA_SPU].CHCR = 0;
    return n < LIMIT;
}
static int s_spuOk;
// mode: 0 plain (request on, no pause), 1 pause nothing written,
//       2 MADR <- B, 3 BCR <- 1 block, 4 both.
static void spuArm(int mode, const char *name) {
    ramsyscall_printf("STEP %s begin\n", name);
    SPUCNT = 0xc000;
    SPU_TCTRL = 0x0004;
    for (int i = 0; i < 16; i++) {
        s_sA[i] = 0xa0000000u | i;
        s_sB[i] = 0xb0000000u | i;
    }
    for (int i = 0; i < 32; i++) s_sFill[i] = 0x5a5a0000u | i;
    spuMode(0);
    SPU_ADDR = SPU_BASE >> 3;
    spuMode(2);
    s_spuOk = spuDma((uintptr_t)s_sFill, (8 << 16) | 4, 0x01000201);
    spuWait();
    spuMode(0);
    SPU_ADDR = SPU_BASE >> 3;
    if (mode == 0) {
        spuMode(2);
        s_spuOk &= spuDma((uintptr_t)s_sA, (4 << 16) | 4, 0x01000201);
    } else {
        DMA_CTRL[DMA_SPU].MADR = (uintptr_t)s_sA;
        DMA_CTRL[DMA_SPU].BCR = (4 << 16) | 4;
        DMA_CTRL[DMA_SPU].CHCR = 0x11000201;
        int i = 0;
        while (i < LIMIT && (DMA_CTRL[DMA_SPU].CHCR & 0x10000000)) i++;
        for (int k = 0; k < 2000; k++) __asm__ volatile("");
        uint32_t m1 = DMA_CTRL[DMA_SPU].MADR, b1 = DMA_CTRL[DMA_SPU].BCR, c1 = DMA_CTRL[DMA_SPU].CHCR;
        if (mode == 2 || mode == 4) DMA_CTRL[DMA_SPU].MADR = (uintptr_t)s_sB;
        if (mode == 3 || mode == 4) DMA_CTRL[DMA_SPU].BCR = (1 << 16) | 4;
        ramsyscall_printf("OBS %s paused MADR %08x BCR %08x CHCR %08x written MADR %08x BCR %08x A %08x B %08x\n", name,
                          m1, b1, c1, DMA_CTRL[DMA_SPU].MADR, DMA_CTRL[DMA_SPU].BCR,
                          (uint32_t)(uintptr_t)s_sA & 0xffffff, (uint32_t)(uintptr_t)s_sB & 0xffffff);
        spuMode(2);
        int n = 0;
        while (n < LIMIT && (DMA_CTRL[DMA_SPU].CHCR & 0x01000000)) n++;
        s_spuOk = n < LIMIT;
        if (!s_spuOk) DMA_CTRL[DMA_SPU].CHCR = 0;
    }
    spuWait();
    ramsyscall_printf("OBS %s end %s MADR %08x BCR %08x CHCR %08x\n", name, s_spuOk ? "done" : "stuck",
                      DMA_CTRL[DMA_SPU].MADR, DMA_CTRL[DMA_SPU].BCR, DMA_CTRL[DMA_SPU].CHCR);
    spuMode(0);
    for (int i = 0; i < 32; i++) s_sOut[i] = 0;
    SPU_ADDR = SPU_BASE >> 3;
    spuMode(3);
    spuDma((uintptr_t)s_sOut, (8 << 16) | 4, 0x01000200);
    spuWait();
    spuMode(0);
    ramsyscall_printf("SPU %s", name);
    for (int i = 0; i < 32; i++) {
        uint32_t w = s_sOut[i];
        if ((w >> 16) == 0x5a5a) ramsyscall_printf(" f%d", w & 0xff);
        else if ((w >> 28) == 0xa) ramsyscall_printf(" A%d", w & 0xff);
        else if ((w >> 28) == 0xb) ramsyscall_printf(" B%d", w & 0xff);
        else ramsyscall_printf(" ?%08x", w);
    }
    ramsyscall_printf("\n");
}

static int s_interruptsWereEnabled;

)

CESTER_BEFORE_ALL(dma_rewrite2,
    s_interruptsWereEnabled = enterCriticalSection();
    DPCR |= 0x00090800;
)

CESTER_AFTER_ALL(dma_rewrite2,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

// Positive controls.
CESTER_TEST(p8, dma_rewrite2, oneBlock(8, 1, "p8"); cester_assert_int_eq(8, s_dmaN);)
CESTER_TEST(s_plain, dma_rewrite2, spuArm(0, "s_plain"); cester_assert_int_eq(1, s_spuOk);)
// GPU: CHCR rewritten between blocks.
CESTER_MAYBE_TEST(c_none, dma_rewrite2, chcrArm(0, "c_none"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(c_same, dma_rewrite2, chcrArm(0x01000201, "c_same"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(c_step, dma_rewrite2, chcrArm(0x01000203, "c_step"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(c_dir, dma_rewrite2, chcrArm(0x01000200, "c_dir"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(c_dirstep, dma_rewrite2, chcrArm(0x01000202, "c_dirstep"); cester_assert_int_eq(1, s_stepOk);)
// SPU: MADR/BCR rewritten between blocks.
CESTER_MAYBE_TEST(s_pause, dma_rewrite2, spuArm(1, "s_pause"); cester_assert_int_eq(1, s_spuOk);)
CESTER_MAYBE_TEST(s_madr, dma_rewrite2, spuArm(2, "s_madr"); cester_assert_int_eq(1, s_spuOk);)
CESTER_MAYBE_TEST(s_bcr, dma_rewrite2, spuArm(3, "s_bcr"); cester_assert_int_eq(1, s_spuOk);)
CESTER_MAYBE_TEST(s_both, dma_rewrite2, spuArm(4, "s_both"); cester_assert_int_eq(1, s_spuOk);)
