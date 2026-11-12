/*

MIT License

Copyright (c) 2023 PCSX-Redux authors

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
 * A GPU DMA (channel 2) linked list used as a ring buffer under load. The
 * ring is RING slots, each one node carrying a full-screen gouraud-shaded,
 * textured, semi-transparent quad (GP0(3Eh)) sampling the 15-bit fixture
 * texture of the gpu-raster tests with bit 15 set on every texel, so every
 * pixel blends, linked in a circle, so the transfer never
 * ends on its own.
 * While it runs, the CPU rewrites the quad colours of the slots behind the
 * read pointer it sees in D2_MADR, trailing it by 2 slots (HIGH) or by half
 * the ring (MID). Every vblank the test records MADR, the slots the CPU
 * rewrote, the laps the DMA made, CHCR and DICR. It then leaves the ring by
 * pointing one link at FFFFFFh and waits for the channel to stop.
 *
 * On a v0 and a v2 console the channel is expected to stay busy with MADR
 * moving, the CPU to keep running, and the exit to complete with the DMA
 * channel 2 flag raised and no DMA error. The asserts on that are
 * hardware-only under PCSX_TESTS: Redux ends a looping list at once.
 */

#include <stdint.h>

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"
#include "texture-fixtures.h"

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
#define D_DPCR R32(0xbf8010f0)
#define D_DICR R32(0xbf8010f4)
#define RING 16
#define SLOT_WORDS 16
#define VBLANKS 10
#define EXIT_LIMIT 2000000

static uint32_t s_ring[RING * SLOT_WORDS] __attribute__((aligned(64)));

struct Arm {
    uint32_t madr[VBLANKS + 1];
    uint32_t chcr[VBLANKS + 1];
    uint32_t dicr[VBLANKS + 1];
    uint16_t written[VBLANKS + 1];
    uint16_t laps[VBLANKS + 1];
    uint32_t exitPolls;
    uint32_t exitChcr, exitMadr, exitDicr, exitIstat;
};

static struct Arm s_high, s_mid;
static int s_interruptsWereEnabled;

static uint32_t slotAddr(unsigned k) { return (uint32_t)&s_ring[k * SLOT_WORDS] & 0xffffff; }

#define QUAD_WORDS 12
// Texpage attribute: the 15-bit fixture cell, B/2+F/2, 15-bit colours.
#define QUAD_TPAGE (TEX15_TX | (2u << 7))

static void writeQuad(unsigned k, uint32_t seq) {
    uint32_t *p = &s_ring[k * SLOT_WORDS + 1];
    uint32_t c = (seq * 0x0f0b07u) & 0x007f7f7fu;
    p[0] = 0x3e000000 | c;
    p[1] = 0;
    p[2] = 0;
    p[3] = c ^ 0x003f003f;
    p[4] = 319;
    p[5] = (QUAD_TPAGE << 16) | 63;
    p[6] = c ^ 0x00003f3f;
    p[7] = 239 << 16;
    p[8] = 15 << 8;
    p[9] = c ^ 0x003f3f00;
    p[10] = (239 << 16) | 319;
    p[11] = (15 << 8) | 63;
}

static void buildRing(void) {
    for (unsigned k = 0; k < RING; k++) {
        s_ring[k * SLOT_WORDS] = ((uint32_t)QUAD_WORDS << 24) | slotAddr((k + 1) % RING);
        writeQuad(k, k);
    }
}

static int slotOf(uint32_t madr) {
    uint32_t base = slotAddr(0);
    madr &= 0xffffff;
    if (madr < base) return -1;
    uint32_t off = (madr - base) / (SLOT_WORDS * 4);
    return off < RING ? (int)off : -1;
}

static void setupGpu(void) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x06000000 | 0x260 | (0xc60 << 12);
    GPU_STATUS = 0x07000000 | 0x010 | (0x100 << 10);
    GPU_STATUS = 0x05000000;
    GPU_STATUS = 0x08000001;
    GPU_DATA = 0xe1000600;
    GPU_DATA = 0xe3000000;
    GPU_DATA = 0xe4000000 | 319 | (239 << 10);
    GPU_DATA = 0xe5000000;
    GPU_STATUS = 0x03000000;
    GPU_STATUS = 0x04000002;
}

static void run(struct Arm *a, unsigned trail, const char *name) {
    setupGpu();
    uploadTex15Masked();
    waitGPU();
    buildRing();
    D_DPCR |= 0x00000800;
    D_DICR = 0x00840000 | 0x7f000000;
    IREG = ~0x8u;
    D2_MADR = slotAddr(0);
    D2_BCR = 0;
    D2_CHCR = 0x01000401;
    unsigned prod = (RING - trail) % RING;
    int last = 0;
    uint32_t seq = RING;
    unsigned written = 0, laps = 0;
    a->madr[0] = D2_MADR;
    a->chcr[0] = D2_CHCR;
    a->dicr[0] = D_DICR;
    a->written[0] = 0;
    a->laps[0] = 0;
    for (int v = 1; v <= VBLANKS; v++) {
        IREG = ~1u;
        while (!(IREG & 1)) {
            int d = slotOf(D2_MADR);
            if (d < 0) continue;
            if (d < last) laps++;
            last = d;
            unsigned target = (d + RING - trail) % RING;
            while (prod != target) {
                writeQuad(prod, seq++);
                prod = (prod + 1) % RING;
                written++;
            }
        }
        a->madr[v] = D2_MADR;
        a->chcr[v] = D2_CHCR;
        a->dicr[v] = D_DICR;
        a->written[v] = written;
        a->laps[v] = laps;
        written = 0;
        laps = 0;
    }
    int d = slotOf(D2_MADR);
    unsigned e = ((d < 0 ? 0 : d) + RING / 2) % RING;
    s_ring[e * SLOT_WORDS] = ((uint32_t)QUAD_WORDS << 24) | 0xffffff;
    uint32_t polls = 0;
    while ((D2_CHCR & 0x01000000) && polls < EXIT_LIMIT) polls++;
    a->exitPolls = polls;
    a->exitChcr = D2_CHCR;
    a->exitMadr = D2_MADR;
    a->exitDicr = D_DICR;
    a->exitIstat = IREG;
    if (a->exitChcr & 0x01000000) D2_CHCR = 0x00000401;
    for (int v = 0; v <= VBLANKS; v++) {
        ramsyscall_printf("%s v=%d madr=%08x slot=%d chcr=%08x dicr=%08x written=%d laps=%d\n", name, v, a->madr[v],
                          slotOf(a->madr[v]), a->chcr[v], a->dicr[v], a->written[v], a->laps[v]);
    }
    ramsyscall_printf("%s exit polls=%d chcr=%08x madr=%08x dicr=%08x istat=%08x\n", name, a->exitPolls, a->exitChcr,
                      a->exitMadr, a->exitDicr, a->exitIstat);
}

static int allBusy(const struct Arm *a) {
    for (int v = 0; v <= VBLANKS; v++)
        if (!(a->chcr[v] & 0x01000000)) return 0;
    return 1;
}

static unsigned minWritten(const struct Arm *a) {
    unsigned m = 0xffff;
    for (int v = 2; v <= VBLANKS; v++)
        if (a->written[v] < m) m = a->written[v];
    return m;
}

static unsigned totalLaps(const struct Arm *a) {
    unsigned n = 0;
    for (int v = 1; v <= VBLANKS; v++) n += a->laps[v];
    return n;
}
)

CESTER_BEFORE_ALL(gpudmaring,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    run(&s_high, 2, "high");
    run(&s_mid, RING / 2, "mid");
)

CESTER_AFTER_ALL(gpudmaring,
    D2_CHCR = 0x00000401;
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_MAYBE_TEST(ringStaysBusy, gpudmaring,
    cester_assert_int_eq(1, allBusy(&s_high));
    cester_assert_int_eq(1, allBusy(&s_mid));
)

CESTER_MAYBE_TEST(dmaLapsTheRing, gpudmaring,
    // An SCPH-1001 consumed about 5 slots per field and an SCPH-1000 about 2,
    // so ten fields wrap the ring three times or once.
    cester_assert_uint_ge(totalLaps(&s_high), 1);
    cester_assert_uint_ge(totalLaps(&s_mid), 1);
)

CESTER_MAYBE_TEST(cpuKeepsProducing, gpudmaring,
    cester_assert_uint_gt(minWritten(&s_high), 0);
    cester_assert_uint_gt(minWritten(&s_mid), 0);
)

CESTER_MAYBE_TEST(noDmaErrorWhileLooping, gpudmaring,
    for (int v = 0; v <= VBLANKS; v++) {
        cester_assert_uint_eq(0, (s_high.dicr[v] & 0x00008000));
        cester_assert_uint_eq(0, (s_mid.dicr[v] & 0x00008000));
    }
)

CESTER_MAYBE_TEST(exitByTerminatorCompletes, gpudmaring,
    cester_assert_uint_eq(0, (s_high.exitChcr & 0x01000000));
    cester_assert_uint_eq(0, (s_mid.exitChcr & 0x01000000));
    cester_assert_uint_eq(0x00ffffff, (s_high.exitMadr & 0x00ffffff));
    cester_assert_uint_eq(0x00ffffff, (s_mid.exitMadr & 0x00ffffff));
)

CESTER_MAYBE_TEST(exitRaisesTheChannelFlag, gpudmaring,
    cester_assert_uint_ne(0, (s_high.exitDicr & 0x04000000));
    cester_assert_uint_ne(0, (s_mid.exitDicr & 0x04000000));
    cester_assert_uint_eq(0, (s_high.exitDicr & 0x00008000));
    cester_assert_uint_eq(0, (s_mid.exitDicr & 0x00008000));
)
