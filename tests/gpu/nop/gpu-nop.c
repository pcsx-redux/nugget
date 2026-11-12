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
 * GP0 no-op opcode probe.
 *
 * psx-spx documents GP0(00h) as "NOP (?)" and GP0(04h..1Eh,E0h,E7h..EFh) as
 * "Mirrors of GP0(00h) - NOP (?)", and says in the same breath that the FIFO
 * argument does not decide it: GP0(E3h..E5h) take no FIFO space either and
 * those have a function. So the question is what, if anything, any of these
 * opcodes does to observable GPU state.
 *
 * Everything GP0(E1h..E6h) sets is readable. GPUSTAT carries the draw-mode and
 * mask bits, and GP1(10h) latches the texture window, drawing area corners and
 * drawing offset into GPUREAD. Between the two, the whole of the GPU's
 * settable state is observable, so a per-opcode before/after diff is a
 * complete test over that surface.
 *
 * Method, per (opcode, parameter):
 *   1. GP1(00h) full reset, so a desync inside one probe cannot reach the next.
 *   2. Write a distinctive known state through GP0(E1h..E6h).
 *   3. Snapshot GPUSTAT + GP1(10h) indices 2,3,4,5.
 *   4. Issue the single probe word.
 *   5. Snapshot again, and print the diff.
 *
 * The sweep deliberately contains opcodes with KNOWN observable effects, so a
 * row of "no change" on the suspects is not a blind null:
 *   GP0(1Fh)     - interrupt request, sets GPUSTAT bit 24. Single word, and
 *                  it sits immediately above the claimed mirror range.
 *   GP0(E1h..E6h)- the real drawing-state commands. Each must move something.
 * If those do not light up, the instrument is broken and every other row in
 * the run is void.
 *
 * GP0(02h) is skipped: it is a three-word fast fill and issuing it with one
 * word would desync the FIFO.
 *
 * GPUSTAT bits that move on their own are masked out of the comparison (see
 * STAT_MASK); the raw value is printed anyway so the mask can be second-
 * guessed from the log rather than trusted.
 */

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

/* bit 13 interlace field, 25 DMA request, 26 cmd ready, 27 VRAM-send ready,
   28 DMA-block ready, 31 drawing even/odd line. All free-running. */
#define STAT_MASK (~((1u << 13) | (1u << 25) | (1u << 26) | (1u << 27) | (1u << 28) | (1u << 31)))

struct Snap {
    uint32_t stat;
    uint32_t r2, r3, r4, r5;
};

static uint32_t readInternal(uint32_t index) {
    GPU_STATUS = 0x10000000 | index;
    return GPU_DATA;
}

/* Bounded, so an opcode that turns out to want parameters cannot hang the run
   the way a bit-26 poll would. Round 1 read GPUSTAT as the FIRST thing after
   the probe write and every GPUSTAT-visible control (GP0(1Fh), GP0(E1h),
   GP0(E6h)) came back "same" while the GP1(10h)-visible ones fired, because
   those four extra bus round-trips were the only settle in the loop. */
static void settle(void) {
    volatile uint32_t sink = 0;
    for (int i = 0; i < 64; i++) sink ^= GPU_STATUS;
    (void)sink;
}

static void takeSnap(struct Snap* s) {
    s->r2 = readInternal(2);
    s->r3 = readInternal(3);
    s->r4 = readInternal(4);
    s->r5 = readInternal(5);
    s->stat = GPU_STATUS;
}

/* Distinctive, and deliberately not the post-reset values. */
static void setKnownState(void) {
    waitGPU();
    GPU_DATA = 0xe1000000 | 0x0005 | (1 << 9) | (1 << 10); /* draw mode */
    GPU_DATA = 0xe2000000 | 0x0005a3;                      /* texture window */
    GPU_DATA = 0xe3000000 | (12 | (34 << 10));             /* draw area TL */
    GPU_DATA = 0xe4000000 | (567 | (321 << 10));           /* draw area BR */
    GPU_DATA = 0xe5000000 | (77 | (88 << 11));             /* draw offset */
    GPU_DATA = 0xe6000000 | 0x3;                           /* mask bits */
}

static void fullReset(void) {
    GPU_STATUS = 0x00000000; /* GP1(00h) */
    GPU_STATUS = 0x03000001; /* display off, keeps the capture card quiet */
}

static int probe(uint32_t op, uint32_t param) {
    struct Snap a, b;
    fullReset();
    setKnownState();
    takeSnap(&a);

    waitGPU();
    GPU_DATA = (op << 24) | param;
    settle();

    takeSnap(&b);

    int changed = 0;
    if (((a.stat ^ b.stat) & STAT_MASK) != 0) changed |= 1;
    if (a.r2 != b.r2) changed |= 2;
    if (a.r3 != b.r3) changed |= 4;
    if (a.r4 != b.r4) changed |= 8;
    if (a.r5 != b.r5) changed |= 16;

    ramsyscall_printf("OP=%02x P=%06x stat=%08x>%08x tw=%08x>%08x tl=%08x>%08x br=%08x>%08x of=%08x>%08x %s\n",
                      op, param, a.stat, b.stat, a.r2, b.r2, a.r3, b.r3, a.r4, b.r4, a.r5, b.r5,
                      changed ? "CHANGED" : "same");
    return changed;
}

/* Fill a rect and read it straight back, so the readback path is proven
   before any absence measured through it is believed. */
static int vramSelfTest(void) {
    waitGPU();
    GPU_DATA = 0x02000000 | 0xf8f8f8; /* fast fill white; 0x7fff whatever the channel order */
    GPU_DATA = (8 << 16) | 16;        /* y=8 x=16 */
    GPU_DATA = (8 << 16) | 32;        /* h=8 w=32 */

    GPU_STATUS = 0x04000001; /* GP1(04h)=1, FIFO mode: bit 27 advances */
    waitGPU();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (8 << 16) | 16;
    GPU_DATA = (8 << 16) | 32;

    uint32_t words = (32 * 8) / 2;
    uint32_t good = 0, first = 0xffffffff;
    for (uint32_t i = 0; i < words; i++) {
        while ((GPU_STATUS & (1 << 27)) == 0);
        uint32_t v = GPU_DATA;
        if (i == 0) first = v;
        if (v == 0x7fff7fff) good++;
    }
    ramsyscall_printf("VRAMSELFTEST words=%d matched=%d first=%08x %s\n", words, good, first,
                      good == words ? "OK" : "BROKEN");
    return good == words;
}

static uint32_t vramChecksum(void) {
    uint32_t sum = 0;
    for (uint32_t band = 0; band < 8; band++) {
        GPU_STATUS = 0x04000001;
        waitGPU();
        GPU_DATA = 0xc0000000;
        GPU_DATA = (0 << 16) | (band * 128);
        GPU_DATA = (512 << 16) | 128;
        for (uint32_t i = 0; i < (128 * 512) / 2; i++) {
            while ((GPU_STATUS & (1 << 27)) == 0);
            sum = (sum * 33) ^ GPU_DATA;
        }
    }
    return sum;
}

/* 64x64 checkerboard of two colours, so a stray solid draw anywhere in VRAM
   cannot land on a matching background everywhere it might go. */
static void paintVram(void) {
    for (uint32_t y = 0; y < 512; y += 64) {
        for (uint32_t x = 0; x < 1024; x += 64) {
            uint32_t c = (((x >> 6) ^ (y >> 6)) & 1) ? 0x0028f8 : 0xf82800;
            waitGPU();
            GPU_DATA = 0x02000000 | c;
            GPU_DATA = (y << 16) | x;
            GPU_DATA = (64 << 16) | 64;
        }
    }
}

/*
 * FIFO occupancy, which is the other half of the page's own argument and the
 * only evidence it offers: "one can send dozens of GP0(00h) commands, without
 * the command FIFO becoming full" while "GP0(03h) does take up space in FIFO".
 *
 * Controls, and both are the page's own claims rather than mine: GP0(03h),
 * documented as taking FIFO space, and GP0(60h), a real 3-word rectangle.
 * Both must come out slow or the column is void.
 *
 * Round 3 tried to read this off GPUSTAT bit 26 and got 0 for all 48 opcodes
 * INCLUDING both controls, because bit 26 already reads low while a blit is
 * executing and before any command has been written. There it is a busy flag,
 * not a FIFO-occupancy flag, and it has no power over this question.
 *
 * So time the burst instead. Start the blit, then write the opcode 64 times
 * back to back with no reads in between, and count root counter 2 (system
 * clock / 8) across the burst. An opcode consuming no FIFO slot is swallowed
 * at bus speed. One that consumes a slot fills the 16-word FIFO behind the
 * busy blit and the CPU stalls on the write port until it retires.
 *
 * An IDLE baseline is printed as the floor for "swallowed at bus speed". If
 * the controls do not separate from that floor, the instrument has no
 * sensitivity and the whole column is void, exactly as in round 3.
 */
static uint32_t burstTicks(uint32_t op, int busy) {
    fullReset();
    GPU_STATUS = 0x04000001;

    if (busy) {
        waitGPU();
        GPU_DATA = 0x80000000;         /* VRAM-to-VRAM blit */
        GPU_DATA = (0 << 16) | 0;      /* src y,x */
        GPU_DATA = (256 << 16) | 0;    /* dst y,x */
        GPU_DATA = (256 << 16) | 512;  /* h,w - big enough to stay busy */
    }

    COUNTERS[2].target = 0xffff;
    COUNTERS[2].mode = TM_CLK_DIV8;
    COUNTERS[2].value = 0;
    for (uint32_t i = 0; i < 64; i++) GPU_DATA = op << 24;
    return COUNTERS[2].value;
}

static const uint8_t kParams = 3;
static const uint32_t kParamValues[3] = {0x000000, 0xffffff, 0x08a16c};

int main(void) {
    ramsyscall_printf("=== GP0 no-op opcode probe ===\n");
    ramsyscall_printf("Compiled: %s %s\n", __DATE__, __TIME__);
    fullReset();
    ramsyscall_printf("GPUVER idx7=%08x idx8=%08x stat=%08x\n", readInternal(7), readInternal(8), GPU_STATUS);
    ramsyscall_printf("STATMASK=%08x\n", STAT_MASK);

    int selftest = vramSelfTest();

    ramsyscall_printf("--- state sweep ---\n");
    int controlsFired = 0;
    for (uint32_t op = 0x00; op <= 0x1f; op++) {
        if (op == 0x02) continue; /* three-word fast fill, would desync */
        for (uint8_t p = 0; p < kParams; p++) {
            int c = probe(op, kParamValues[p]);
            if (op == 0x1f && c) controlsFired++;
        }
    }
    for (uint32_t op = 0xe0; op <= 0xef; op++) {
        for (uint8_t p = 0; p < kParams; p++) {
            int c = probe(op, kParamValues[p]);
            if (op >= 0xe1 && op <= 0xe6 && c) controlsFired++;
        }
    }
    ramsyscall_printf("CONTROLS fired=%d of 21 expected\n", controlsFired);

    ramsyscall_printf("--- fifo sweep (counter2 ticks for 64 back-to-back writes) ---\n");
    ramsyscall_printf("FIFO OP=00 idle=%d (floor)\n", burstTicks(0x00, 0));
    ramsyscall_printf("FIFO OP=03 idle=%d (floor)\n", burstTicks(0x03, 0));
    ramsyscall_printf("FIFO OP=60 idle=%d (floor)\n", burstTicks(0x60, 0));
    for (uint32_t op = 0x00; op <= 0x1f; op++) {
        if (op == 0x02) continue;
        ramsyscall_printf("FIFO OP=%02x busy=%d\n", op, burstTicks(op, 1));
    }
    for (uint32_t op = 0xe0; op <= 0xef; op++) {
        ramsyscall_printf("FIFO OP=%02x busy=%d\n", op, burstTicks(op, 1));
    }
    ramsyscall_printf("FIFO OP=60 busy=%d (3-word rect, second control)\n", burstTicks(0x60, 1));

    ramsyscall_printf("--- vram sweep ---\n");
    paintVram();
    /* The checksum has to be shown SENSITIVE before its "unchanged" means
       anything: one 8x8 fill, well inside one checkerboard square, must move
       it. Then repaint and re-take the baseline. */
    uint32_t probeSum = vramChecksum();
    waitGPU();
    GPU_DATA = 0x02000000 | 0x00ff00;
    GPU_DATA = (300 << 16) | 500;
    GPU_DATA = (8 << 16) | 8;
    uint32_t perturbed = vramChecksum();
    ramsyscall_printf("VRAMCTRL base=%08x perturbed=%08x %s\n", probeSum, perturbed,
                      probeSum != perturbed ? "SENSITIVE" : "BLIND");
    paintVram();

    uint32_t before = vramChecksum();
    for (uint32_t op = 0x00; op <= 0x1f; op++) {
        if (op == 0x02) continue;
        for (uint8_t p = 0; p < kParams; p++) {
            fullReset();
            setKnownState();
            waitGPU();
            GPU_DATA = (op << 24) | kParamValues[p];
        }
    }
    for (uint32_t op = 0xe0; op <= 0xef; op++) {
        for (uint8_t p = 0; p < kParams; p++) {
            fullReset();
            setKnownState();
            waitGPU();
            GPU_DATA = (op << 24) | kParamValues[p];
        }
    }
    uint32_t after = vramChecksum();
    ramsyscall_printf("VRAM before=%08x after=%08x %s\n", before, after,
                      before == after ? "UNTOUCHED" : "MODIFIED");

    ramsyscall_printf("DONE selftest=%d controls=%d\n", selftest, controlsFired);
    while (1);
    return 0;
}
