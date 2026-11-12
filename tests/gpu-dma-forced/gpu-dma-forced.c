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

// GPU DMA (channel 2), sync mode 1, RAM to VRAM. Two questions about what a
// block forced with CHCR bit 28 (GP1(04h)=0, no DREQ) delivers to VRAM, and
// about words missing from the end of a transfer that leaves its VRAM
// rectangle short.
//
//   F/P arms: one block of N = 1, 2, 4, 8, 16 words into a 16x8 rectangle,
//     forced (F) or DREQ-paced with GP1(04h)=2 (P). The CPU then writes 64
//     filler words with GP1(04h)=1, polling GPUSTAT.25, so the rectangle
//     always completes; filler past its end decodes as GP0(00h). The report
//     is the source index of every DMA word that landed before the first
//     filler word. P arms are the positive control: all N must land.
//   T arms: 4 blocks of 8 into a 16x4 rectangle, block 0 forced, then
//     resumed with GP1(04h)=2. Then: nothing (t_none), 4 or 8 CPU filler
//     words (t_flush4, t_flush8), or a long wait (t_wait), before the
//     GP1(00h) that precedes the readback.
//   U arms: 3 paced blocks of 8 into the same 16x4 rectangle, no forced
//     block, with and without 8 filler words.
//   H arms: as T with blocks of 4 and 16 words.
//   S arms: one paced block of k words into a w x h rectangle, nothing after.
//
// On an SCPH-1000, 1001, 5501 and 7001 a forced block writes words 0, 2, 4...
// only, packed, at every size, and MADR still advances by the whole block. On
// the v2 GPUs (1001, 5501, 7001) CPU-to-VRAM data is written one complete
// rectangle row at a time and GP1(00h) discards a partial last row; the
// SCPH-1000 writes every word. Redux writes nothing for a forced block with
// GP1(04h)=0 and loses a short rectangle entirely, so those arms only run on
// hardware.

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

static void tail(int forced, int mode, const char *name) { tailN(forced, 8, mode, name); }

static int s_interruptsWereEnabled;

)

CESTER_BEFORE_ALL(gpu_dma_forced,
    s_interruptsWereEnabled = enterCriticalSection();
    DPCR |= 0x00000800;
)

CESTER_AFTER_ALL(gpu_dma_forced,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

// Positive controls: a DREQ-paced block lands whole, in order, then filler.
CESTER_TEST(p1, gpu_dma_forced, oneBlock(1, 1, "p1"); cester_assert_int_eq(1, s_dmaN); cester_assert_int_eq(1, s_ordered); cester_assert_int_eq(0, s_junk); cester_assert_int_eq(0, s_guardBad);)
CESTER_TEST(p2, gpu_dma_forced, oneBlock(2, 1, "p2"); cester_assert_int_eq(2, s_dmaN); cester_assert_int_eq(1, s_ordered); cester_assert_int_eq(0, s_junk);)
CESTER_TEST(p4, gpu_dma_forced, oneBlock(4, 1, "p4"); cester_assert_int_eq(4, s_dmaN); cester_assert_int_eq(1, s_ordered); cester_assert_int_eq(0, s_junk);)
CESTER_TEST(p8, gpu_dma_forced, oneBlock(8, 1, "p8"); cester_assert_int_eq(8, s_dmaN); cester_assert_int_eq(1, s_ordered); cester_assert_int_eq(0, s_junk);)
CESTER_TEST(p16, gpu_dma_forced, oneBlock(16, 1, "p16"); cester_assert_int_eq(16, s_dmaN); cester_assert_int_eq(1, s_ordered); cester_assert_int_eq(0, s_junk); cester_assert_int_eq(0, s_guardBad);)
// Observation arms, hardware only: read the IDX and RESULT lines.
CESTER_MAYBE_TEST(f1, gpu_dma_forced, oneBlock(1, 0, "f1"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(f2, gpu_dma_forced, oneBlock(2, 0, "f2"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(f4, gpu_dma_forced, oneBlock(4, 0, "f4"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(f8, gpu_dma_forced, oneBlock(8, 0, "f8"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(f16, gpu_dma_forced, oneBlock(16, 0, "f16"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(t_none, gpu_dma_forced, tail(1, 0, "t_none"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(t_flush4, gpu_dma_forced, tail(1, 1, "t_flush4"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(t_flush8, gpu_dma_forced, tail(1, 2, "t_flush8"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(t_wait, gpu_dma_forced, tail(1, 3, "t_wait"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(u_none, gpu_dma_forced, tail(0, 0, "u_none"); cester_assert_int_eq(1, s_done);)
CESTER_TEST(u_flush8, gpu_dma_forced, tail(0, 2, "u_flush8"); cester_assert_int_eq(1, s_done);)
// Held tail against the forced block size: blocks of 4 and 16 words.
CESTER_MAYBE_TEST(h4_none, gpu_dma_forced, tailN(1, 4, 0, "h4_none"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(h4_flush16, gpu_dma_forced, tailN(1, 4, 4, "h4_flush16"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(h16_none, gpu_dma_forced, tailN(1, 16, 0, "h16_none"); cester_assert_int_eq(1, s_stepOk);)
CESTER_MAYBE_TEST(h16_flush16, gpu_dma_forced, tailN(1, 16, 4, "h16_flush16"); cester_assert_int_eq(1, s_stepOk);)
// Partial last unit: row of the rect, or 8 words (16 pixels)?
CESTER_MAYBE_TEST(s16_k12, gpu_dma_forced, shortRect(16, 4, 12, "s16_k12"); cester_assert_int_eq(1, s_done);)
CESTER_MAYBE_TEST(s32_k12, gpu_dma_forced, shortRect(32, 2, 12, "s32_k12"); cester_assert_int_eq(1, s_done);)
CESTER_MAYBE_TEST(s32_k28, gpu_dma_forced, shortRect(32, 2, 28, "s32_k28"); cester_assert_int_eq(1, s_done);)
CESTER_MAYBE_TEST(s8_k6, gpu_dma_forced, shortRect(8, 8, 6, "s8_k6"); cester_assert_int_eq(1, s_done);)
CESTER_MAYBE_TEST(s8_k10, gpu_dma_forced, shortRect(8, 8, 10, "s8_k10"); cester_assert_int_eq(1, s_done);)
CESTER_TEST(s32_k32, gpu_dma_forced, shortRect(32, 2, 32, "s32_k32"); cester_assert_int_eq(1, s_done); cester_assert_int_eq(32, s_dmaN); cester_assert_int_eq(0, s_guardBad);)
