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
// already sent. Rectangles, blocks of whole commands, GP1(04h)=1 and linked
// lists all complete. Passes on an SCPH-5501 and an SCPH-7001.

#include <stdint.h>

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
