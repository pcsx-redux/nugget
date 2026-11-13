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
 * GPU DMA (channel 2) in linked-list mode on a two-node list A -> B -> A that
 * never reaches the FFFFFFh terminator, against what an SCPH-1000 and an
 * SCPH-1001 were both measured to do: the channel stays busy (CHCR bit 24)
 * across five vblanks while MADR keeps moving between the two nodes, DICR
 * bit 15 (DMA error) stays clear, and writing CHCR with bit 24 clear stops
 * it. Arms: nodes carrying no words, and nodes carrying one GP0(00h) NOP.
 * A terminated list is the control: it completes with MADR = FFFFFFh.
 */

#include <stdint.h>

// Redux ends a looping list on its endless-loop guard, which games that build a loop on purpose
// and edit it to get out rely on, so the three loop asserts are hardware-only under PCSX_TESTS.
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
#define R32(a) (*(volatile uint32_t *)(a))
#define D2_MADR R32(0xbf8010a0)
#define D2_BCR R32(0xbf8010a4)
#define D2_CHCR R32(0xbf8010a8)
#define D2_DPCR R32(0xbf8010f0)
#define D2_DICR R32(0xbf8010f4)
#define VBLANKS 5

static uint32_t s_nodeA[2] __attribute__((aligned(16)));
static uint32_t s_nodeB[2] __attribute__((aligned(16)));

struct Run {
    uint32_t chcr[VBLANKS + 1];
    uint32_t madr[VBLANKS + 1];
    uint32_t dicr[VBLANKS + 1];
    uint32_t chcrAfterStop;
    unsigned madrChanges;
};

static struct Run s_term, s_loop0, s_loop1;
static int s_interruptsWereEnabled;

static void run(struct Run *r, unsigned words, int terminated) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x04000002;
    uint32_t a = (uint32_t)s_nodeA & 0xffffff, b = (uint32_t)s_nodeB & 0xffffff;
    s_nodeA[0] = (words << 24) | b;
    s_nodeA[1] = 0;
    s_nodeB[0] = (words << 24) | (terminated ? 0xffffff : a);
    s_nodeB[1] = 0;
    D2_DPCR |= 0x00000800;
    D2_MADR = a;
    D2_BCR = 0;
    D2_CHCR = 0x01000401;
    r->chcr[0] = D2_CHCR;
    r->madr[0] = D2_MADR;
    r->dicr[0] = D2_DICR;
    r->madrChanges = 0;
    uint32_t last = r->madr[0];
    for (int v = 1; v <= VBLANKS; v++) {
        IREG = ~1u;
        while (!(IREG & 1)) {
            uint32_t m = D2_MADR;
            if (m != last) {
                r->madrChanges++;
                last = m;
            }
        }
        r->chcr[v] = D2_CHCR;
        r->madr[v] = D2_MADR;
        r->dicr[v] = D2_DICR;
    }
    D2_CHCR = 0x00000401;
    r->chcrAfterStop = D2_CHCR;
    ramsyscall_printf("words=%d terminated=%d chcr0=%08x chcr5=%08x madr0=%08x madr5=%08x dicr5=%08x changes=%d stop=%08x\n",
                      words, terminated, r->chcr[0], r->chcr[VBLANKS], r->madr[0], r->madr[VBLANKS], r->dicr[VBLANKS],
                      r->madrChanges, r->chcrAfterStop);
}
)

CESTER_BEFORE_ALL(gpudmaloop,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    run(&s_term, 1, 1);
    run(&s_loop0, 0, 0);
    run(&s_loop1, 1, 0);
)

CESTER_AFTER_ALL(gpudmaloop,
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(terminatedListCompletes, gpudmaloop,
    cester_assert_uint_eq(0, (s_term.chcr[VBLANKS] & 0x01000000));
    cester_assert_uint_eq(0x00ffffff, (s_term.madr[VBLANKS] & 0x00ffffff));
)

CESTER_MAYBE_TEST(loopStaysBusy, gpudmaloop,
    for (int v = 0; v <= VBLANKS; v++) {
        cester_assert_uint_ne(0, (s_loop0.chcr[v] & 0x01000000));
        cester_assert_uint_ne(0, (s_loop1.chcr[v] & 0x01000000));
    }
)

CESTER_MAYBE_TEST(loopKeepsWalking, gpudmaloop,
    cester_assert_uint_gt(s_loop0.madrChanges, 0);
    cester_assert_uint_gt(s_loop1.madrChanges, 0);
)

CESTER_MAYBE_TEST(loopRaisesNoDmaError, gpudmaloop,
    cester_assert_uint_eq(0, (s_loop0.dicr[VBLANKS] & 0x00008000));
    cester_assert_uint_eq(0, (s_loop1.dicr[VBLANKS] & 0x00008000));
)

CESTER_TEST(chcrWriteStopsTheLoop, gpudmaloop,
    cester_assert_uint_eq(0, (s_loop0.chcrAfterStop & 0x01000000));
    cester_assert_uint_eq(0, (s_loop1.chcrAfterStop & 0x01000000));
)
