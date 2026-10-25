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

// GPU DMA in sync mode 1 waits for GPUSTAT.28 before each block. With
// GP1(04h)=2 that bit drops after a polygon or line command word and stays low
// until the command is complete, so a block boundary inside one of those
// stops the channel for good: CHCR stays busy, MADR and BCR show the blocks
// already sent. Poly-lines are in that class too. Rectangles, blocks of whole
// commands, GP1(04h)=1 and linked lists all complete. A CPU-fed poly-line
// longer than the FIFO streams if each word waits for GPUSTAT.25 and loses
// words if it does not. Passes on an SCPH-1000, an SCPH-5501 and an SCPH-7001.

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
#define DONE 0xffffffffu

static uint32_t buf[512] __attribute__((aligned(4)));
static uint32_t node[257] __attribute__((aligned(4)));
static uint32_t seed;

static int rnd(int n) {
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 8) % (unsigned)n);
}
static uint32_t xy(int x, int y) { return ((uint32_t)(uint16_t)(int16_t)y << 16) | (uint16_t)(int16_t)x; }

static void gpuSetup(void) {
    GP1 = 0x00000000;
    GP1 = 0x01000000;
    GP1 = 0x03000000;
    GP1 = 0x08000001;
    GP1 = 0x06c60260;
    GP1 = 0x07040010;
    GP0 = 0xe1000400;
    GP0 = 0xe3000000;
    GP0 = 0xe4000000 | (239 << 10) | 319;
    GP0 = 0xe5000000;
    GP0 = 0xe6000000;
}

// Packet generators fill buf and return the word count. nops GP0(00h) words go
// first, to move the block boundaries.
static int lines(int n, int nops) {
    int w = 0;
    seed = 12345;
    for (int i = 0; i < nops; i++) buf[w++] = 0;
    for (int i = 0; i < n; i++) {
        int x = 10 + rnd(300), y = 10 + rnd(220);
        buf[w++] = 0x40ff8040;
        buf[w++] = xy(x, y);
        buf[w++] = xy(x + rnd(9) - 4, y + rnd(9) - 4);
    }
    return w;
}
static int glines(int n) {
    int w = 0;
    seed = 12345;
    for (int i = 0; i < n; i++) {
        int x = 10 + rnd(300), y = 10 + rnd(220);
        buf[w++] = 0x500000ff;
        buf[w++] = xy(x, y);
        buf[w++] = 0x00ff0000;
        buf[w++] = xy(x + 5, y + 3);
    }
    return w;
}
static int rects(int n) {
    int w = 0;
    seed = 12345;
    for (int i = 0; i < n; i++) {
        buf[w++] = 0x6000c0c0;
        buf[w++] = xy(rnd(310), rnd(230));
        buf[w++] = xy(4, 3);
    }
    return w;
}
static int tris(int n, int nops) {
    int w = 0;
    seed = 12345;
    for (int i = 0; i < nops; i++) buf[w++] = 0;
    for (int i = 0; i < n; i++) {
        int x = 10 + rnd(290), y = 10 + rnd(210);
        buf[w++] = 0x2000c040;
        buf[w++] = xy(x, y);
        buf[w++] = xy(x + 8, y + 1);
        buf[w++] = xy(x + 1, y + 6);
    }
    return w;
}
static int gtris(int n) {
    int w = 0;
    seed = 12345;
    for (int i = 0; i < n; i++) {
        int x = 10 + rnd(290), y = 10 + rnd(210);
        buf[w++] = 0x300000ff;
        buf[w++] = xy(x, y);
        buf[w++] = 0x0000ff00;
        buf[w++] = xy(x + 8, y + 1);
        buf[w++] = 0x00ff0000;
        buf[w++] = xy(x + 1, y + 6);
    }
    return w;
}

// Poly-lines: zigzag segments 290 pixels wide, three rows apart. nops GP0(00h)
// words go first. Ends with the 55555555h terminator.
static int plines(int n, int nops) {
    int w = 0;
    for (int i = 0; i < nops; i++) buf[w++] = 0;
    buf[w++] = 0x48ff8040;
    for (int i = 0; i < n; i++) buf[w++] = xy((i & 1) ? 300 : 10, 10 + i * 3);
    buf[w++] = 0x55555555;
    return w;
}
static int gplines(int n, int nops) {
    int w = 0;
    for (int i = 0; i < nops; i++) buf[w++] = 0;
    for (int i = 0; i < n; i++) {
        if (i) buf[w++] = (i & 1) ? 0x0000ff00 : 0x00ff0000;
        else buf[w++] = 0x580000ff;
        buf[w++] = xy((i & 1) ? 300 : 10, 10 + i * 3);
    }
    buf[w++] = 0x55555555;
    return w;
}

static uint16_t readPx(int x, int y) {
    for (int i = 0; i < LIMIT && !(GP1 & 0x04000000); i++);
    GP0 = 0xc0000000;
    GP0 = xy(x, y);
    GP0 = xy(2, 1);
    for (int i = 0; i < LIMIT && !(GP1 & 0x08000000); i++);
    return GP0 & 0xffff;
}

// CPU-fed poly-line, GP1(04h)=1. paced: wait for GPUSTAT.25 before each word.
// Records GPUSTAT bits 25/26/28 right after each word and the polls spent
// waiting, then reads back a pixel on the last segment.
static uint8_t s_bits[512];
static uint16_t s_waits[512];
static int cpuPline(int n, int paced, uint32_t *maxWait, int *fullAt, uint16_t *lastPx) {
    gpuSetup();
    GP1 = 0x04000001;
    GP0 = 0x02000000;
    GP0 = 0;
    GP0 = xy(320, 240);
    for (int i = 0; i < LIMIT && (GP1 & 0x14000000) != 0x14000000; i++);
    int words = plines(n, 0);
    *maxWait = 0;
    *fullAt = -1;
    for (int i = 0; i < words; i++) {
        uint32_t polls = 0;
        if (paced) while (!(GP1 & 0x02000000) && polls < LIMIT) polls++;
        GP0 = buf[i];
        uint32_t st = GP1;
        s_bits[i] = ((st >> 25) & 1) | (((st >> 26) & 1) << 1) | (((st >> 28) & 1) << 2);
        s_waits[i] = polls > 0xffff ? 0xffff : polls;
        if (polls > *maxWait) *maxWait = polls;
        if (*fullAt < 0 && !(st & 0x02000000)) *fullAt = i;
    }
    for (int i = 0; i < LIMIT && (GP1 & 0x14000000) != 0x14000000; i++);
    *lastPx = readPx(((n - 1) & 1) ? 300 : 10, 10 + (n - 1) * 3);
    ramsyscall_printf("OBS cpu pline n %d paced %d words %d fullAt %d maxWait %d lastpx %04x\nOBS bits", n, paced, words,
                      *fullAt, (int)*maxWait, *lastPx);
    for (int i = 0; i < words; i++) ramsyscall_printf(" %d", s_bits[i]);
    ramsyscall_printf("\nOBS waits");
    for (int i = 0; i < words; i++) ramsyscall_printf(" %d", s_waits[i]);
    ramsyscall_printf("\n");
    GP1 = 0;
    return words;
}

static int waitDma(void) {
    for (int i = 0; i < LIMIT; i++) {
        if ((DMA_CTRL[DMA_GPU].CHCR & 0x01000000) == 0) return 1;
    }
    return 0;
}

// Runs a block transfer of buf[0..words) and checks it against silicon:
// sent == DONE means it completes, anything else is the number of words the
// channel sent before it stopped.
static void block(int words, uint32_t bs, uint32_t dir, uint32_t sent) {
    gpuSetup();
    GP1 = 0x04000000 | dir;
    uint32_t ba = words / bs;
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)buf;
    DMA_CTRL[DMA_GPU].BCR = (ba << 16) | bs;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    int ok = waitDma();
    uint32_t stat = GP1;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    ramsyscall_printf("OBS words %d bs %d dir %d: %s GPUSTAT %08x MADR %08x BCR %08x CHCR %08x\n", words, (int)bs,
                      (int)dir, ok ? "done" : "stuck", stat, madr, bcr, chcr);
    DMA_CTRL[DMA_GPU].CHCR = 0;
    GP1 = 0;
    if (sent == DONE) {
        cester_assert_int_eq(1, ok);
        return;
    }
    cester_assert_int_eq(0, ok);
    cester_assert_uint_eq(0x01000201, chcr);
    // cester's comparisons do not parenthesise their operands, so no operators in the arguments.
    uint32_t expectMadr = ((uintptr_t)buf + sent * 4) & 0xffffff;
    uint32_t expectBcr = ((ba - sent / bs) << 16) | bs;
    uint32_t dreq = stat & 0x10000000;
    cester_assert_uint_eq(expectMadr, madr);
    cester_assert_uint_eq(expectBcr, bcr);
    cester_assert_uint_eq(0, dreq);
}

static void linked(int words) {
    gpuSetup();
    GP1 = 0x04000002;
    node[0] = ((uint32_t)words << 24) | 0x00ffffff;
    for (int i = 0; i < words; i++) node[1 + i] = buf[i];
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)node;
    DMA_CTRL[DMA_GPU].BCR = 0;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    int ok = waitDma();
    ramsyscall_printf("OBS linked words %d: %s CHCR %08x\n", words, ok ? "done" : "stuck", DMA_CTRL[DMA_GPU].CHCR);
    DMA_CTRL[DMA_GPU].CHCR = 0;
    GP1 = 0;
    cester_assert_int_eq(1, ok);
}

static int s_interruptsWereEnabled;

)

CESTER_BEFORE_ALL(gpu_dma_dreq,
    s_interruptsWereEnabled = enterCriticalSection();
    DPCR |= 0x00000800;
)

CESTER_AFTER_ALL(gpu_dma_dreq,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(rects_bs16_split, gpu_dma_dreq, block(rects(80), 16, 2, DONE);)
CESTER_TEST(rects_bs1, gpu_dma_dreq, block(rects(80), 1, 2, DONE);)
CESTER_TEST(lines_bs16_split, gpu_dma_dreq, block(lines(80, 0), 16, 2, 16);)
CESTER_TEST(lines_bs1, gpu_dma_dreq, block(lines(80, 0), 1, 2, 1);)
CESTER_TEST(lines_bs3_whole, gpu_dma_dreq, block(lines(80, 0), 3, 2, DONE);)
CESTER_TEST(lines_bs15_whole, gpu_dma_dreq, block(lines(80, 0), 15, 2, DONE);)
CESTER_TEST(lines_dir1_bs1, gpu_dma_dreq, block(lines(80, 0), 1, 1, DONE);)
CESTER_TEST(lines_dir1_bs16, gpu_dma_dreq, block(lines(80, 0), 16, 1, DONE);)
CESTER_TEST(glines_bs16_whole, gpu_dma_dreq, block(glines(60), 16, 2, DONE);)
CESTER_TEST(glines_bs4_whole, gpu_dma_dreq, block(glines(60), 4, 2, DONE);)
CESTER_TEST(tris_bs16_whole, gpu_dma_dreq, block(tris(60, 0), 16, 2, DONE);)
CESTER_TEST(tris_nop_bs1, gpu_dma_dreq, block(tris(60, 1), 1, 2, 2);)
CESTER_TEST(tris_2nop_bs2_split, gpu_dma_dreq, block(tris(60, 2), 2, 2, 4);)
CESTER_TEST(gtris_bs16_split, gpu_dma_dreq, block(gtris(40), 16, 2, 16);)
CESTER_TEST(gtris_bs6_whole, gpu_dma_dreq, block(gtris(40), 6, 2, DONE);)
CESTER_TEST(gtris_dir1_bs1, gpu_dma_dreq, block(gtris(40), 1, 1, DONE);)
CESTER_TEST(linked_lines_240, gpu_dma_dreq, linked(lines(80, 0));)
CESTER_TEST(linked_lines_12, gpu_dma_dreq, linked(lines(4, 0));)
CESTER_TEST(linked_gtris_240, gpu_dma_dreq, linked(gtris(40));)
CESTER_TEST(linked_rects_240, gpu_dma_dreq, linked(rects(80));)
// GPUSTAT.25 drops after 11 queued words, then each further word waits for
// one 290-pixel segment: the poly-line streams through the FIFO. Bits 26 and
// 28 read 0 after every word of the list. Redux has no GP0 FIFO, so these are
// hardware-only.
CESTER_MAYBE_TEST(pline_cpu_paced, gpu_dma_dreq,
    uint32_t maxWait; int fullAt; uint16_t px;
    int words = cpuPline(60, 1, &maxWait, &fullAt, &px);
    cester_assert_uint_eq(0x7e08, px);
    cester_assert_int_eq(-1, fullAt);
    uint32_t w10 = s_waits[10], w11 = s_waits[11];
    cester_assert_uint_eq(0, w10);
    cester_assert_uint_ne(0, w11);
    int busy = 0;
    for (int i = 1; i < words; i++) busy |= s_bits[i] & 6;
    cester_assert_int_eq(0, busy);
)
// Unpaced, the FIFO is full from the 11th word and the list loses words: the
// last vertex is not drawn where the list puts it.
CESTER_MAYBE_TEST(pline_cpu_unpaced, gpu_dma_dreq,
    uint32_t maxWait; int fullAt; uint16_t px;
    cpuPline(60, 0, &maxWait, &fullAt, &px);
    cester_assert_int_eq(10, fullAt);
    cester_assert_uint_ne(0x7e08, px);
)
// A poly-line is in the DREQ stall class: the block after the one holding its
// command word never starts, whatever the block size.
CESTER_TEST(pline_bs16_split, gpu_dma_dreq, block(plines(46, 0), 16, 2, 16);)
CESTER_TEST(pline_bs1, gpu_dma_dreq, block(plines(46, 0), 1, 2, 1);)
CESTER_TEST(pline_dir1_bs1, gpu_dma_dreq, block(plines(46, 0), 1, 1, DONE);)
CESTER_TEST(gpline_bs16_split, gpu_dma_dreq, block(gplines(23, 1), 16, 2, 16);)
