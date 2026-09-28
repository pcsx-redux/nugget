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
 * GP0 FIFO occupancy, per opcode.
 *
 * tests/gpu-nop settled that GP0(00h), (01h), (03h..1Fh) and (E0h..EFh)
 * leave no readable trace, and failed twice to measure whether they take
 * FIFO space. Neither of its instruments proved the blit it hid behind was
 * running. This one does that first, then reads occupancy off GPUSTAT with
 * GP1(04h)=1, where bit 25 is "GP0 write FIFO not full" and bit 28 is "GP0
 * write FIFO empty".
 *
 * Per opcode:
 *   1. GP1(00h), GP1(04h)=1.
 *   2. Start a 512x256 VRAM-to-VRAM copy.
 *   3. Write the opcode word once per step, up to 32 times, reading GPUSTAT
 *      after each write. Record the first write at which bit 28 drops (the
 *      word went into the FIFO) and the first at which bit 25 drops (FIFO
 *      full).
 *   4. Read GPUSTAT bit 26 at the end: 0 means the copy was still executing
 *      when the last word went in, so every word was written behind it.
 *
 * Controls, which must fill the FIFO or the column is void:
 *   - GP0(A0h) CPU-to-VRAM with a large rectangle: its data words are
 *     FIFO traffic by construction.
 *   - GP0(60h) 3-word rectangles, written word by word.
 *   - GP0(03h), which the page says takes FIFO space.
 * And a proof the copy runs: source and destination painted different
 * colours, a destination pixel read before and after, and the number of
 * GPUSTAT polls until bit 26 comes back.
 */

#include <stdint.h>

#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define ST_FIFO_NOT_FULL (1u << 25)
#define ST_CMD_READY (1u << 26)
#define ST_FIFO_EMPTY (1u << 28)

static void fullReset(void) {
    GPU_STATUS = 0x00000000; /* GP1(00h) */
    GPU_STATUS = 0x03000001; /* display off */
    GPU_STATUS = 0x04000001; /* GP1(04h)=1: bit 25 = FIFO not full */
}

static void waitIdle(void) {
    for (int i = 0; i < 2000000; i++) {
        uint32_t s = GPU_STATUS;
        if ((s & ST_CMD_READY) && (s & ST_FIFO_EMPTY)) return;
    }
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
    waitIdle();
    GPU_DATA = 0x02000000 | c;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (h << 16) | w;
    waitIdle();
}

static uint32_t readPixelPair(uint32_t x, uint32_t y) {
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (1 << 16) | 2;
    for (int i = 0; i < 100000 && !(GPU_STATUS & (1u << 27)); i++);
    uint32_t v = GPU_DATA;
    waitIdle();
    return v;
}

static void startCopy(void) {
    waitIdle();
    GPU_DATA = 0x80000000;        /* VRAM-to-VRAM copy */
    GPU_DATA = (0 << 16) | 0;     /* src y,x */
    GPU_DATA = (256 << 16) | 0;   /* dst y,x */
    GPU_DATA = (256 << 16) | 512; /* h,w */
}

/* Prove the copy executes, and how long it keeps the GPU busy. */
static void copyProof(void) {
    fullReset();
    fill(0, 0, 512, 256, 0x0000f8);   /* source: red */
    fill(0, 256, 512, 256, 0xf80000); /* destination: blue */
    uint32_t before = readPixelPair(300, 400);
    startCopy();
    uint32_t s0 = GPU_STATUS;
    uint32_t polls = 0;
    while (!(GPU_STATUS & ST_CMD_READY) && polls < 10000000) polls++;
    waitIdle();
    uint32_t after = readPixelPair(300, 400);
    ramsyscall_printf("COPY stat0=%08x busyPolls=%d dst %08x -> %08x src=%08x %s\n", s0, polls, before, after,
                      readPixelPair(300, 144), (after != before && after == readPixelPair(300, 144)) ? "COPIED" : "NOT-COPIED");
}

struct Result {
    int firstNotEmpty;
    int firstFull;
    uint32_t endStat;
};

/* words[] is written cyclically, one word per step. */
static struct Result run(const uint32_t* words, int nwords, int prefix, int steps) {
    struct Result r = {-1, -1, 0};
    fullReset();
    startCopy();
    for (int i = 0; i < prefix; i++) GPU_DATA = words[i];
    for (int i = 0; i < steps; i++) {
        GPU_DATA = words[prefix + (i % (nwords - prefix))];
        uint32_t s = GPU_STATUS;
        if (r.firstNotEmpty < 0 && !(s & ST_FIFO_EMPTY)) r.firstNotEmpty = i + 1;
        if (r.firstFull < 0 && !(s & ST_FIFO_NOT_FULL)) r.firstFull = i + 1;
    }
    r.endStat = GPU_STATUS;
    waitIdle();
    return r;
}

static void report(const char* tag, uint32_t op, struct Result r) {
    ramsyscall_printf("FIFO %-6s OP=%02x notEmptyAt=%d fullAt=%d endStat=%08x %s\n", tag, op, r.firstNotEmpty,
                      r.firstFull, r.endStat, (r.endStat & ST_CMD_READY) ? "COPY-DONE-EARLY" : "copy-busy");
}

int main(void) {
    ramsyscall_printf("GPUFIFO-START\n");
    fullReset();
    ramsyscall_printf("IDLE stat=%08x\n", GPU_STATUS);
    /* GP1(10h) index 7: 00000002h on v2, the stale GPUREAD value on v0. */
    GPU_STATUS = 0x10000007;
    ramsyscall_printf("GPUVER idx7=%08x\n", GPU_DATA);
    copyProof();
    copyProof();

    /* Control 1: CPU-to-VRAM data words. Header + 2 params as prefix. */
    {
        uint32_t w[4] = {0xa0000000, (300 << 16) | 600, (200 << 16) | 400, 0x12341234};
        report("A0data", 0xa0, run(w, 4, 3, 32));
    }
    /* Control 2: 3-word rectangles. */
    {
        uint32_t w[3] = {0x60ffffff, (40 << 16) | 700, (8 << 16) | 8};
        report("rect60", 0x60, run(w, 3, 0, 32));
    }
    /* Opcodes under test, parameter 0, plus 03h as the page's own control. */
    static const uint8_t ops[] = {0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
                                  0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                  0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4,
                                  0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef};
    for (unsigned i = 0; i < sizeof(ops); i++) {
        uint32_t w[1] = {(uint32_t)ops[i] << 24};
        report("op", ops[i], run(w, 1, 0, 32));
    }
    /* Controls again at the end. */
    {
        uint32_t w[4] = {0xa0000000, (300 << 16) | 600, (200 << 16) | 400, 0x12341234};
        report("A0data", 0xa0, run(w, 4, 3, 32));
    }
    ramsyscall_printf("GPUFIFO-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
