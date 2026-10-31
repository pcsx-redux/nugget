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
 * GPUSTAT bit 26 (ready to receive a command word) after every word of the
 * multi-word GP0 commands.
 *
 * Each command runs after GP1(00h). After every word, GPUSTAT is read once,
 * then polled for up to POLL_CAP reads until bit 26 is set. A word after
 * which bit 26 never comes back means the GPU is still collecting that
 * command's words; a word after which it comes back within the cap means
 * the command was taken whole (the polls count any drawing time). The run
 * repeats with GP1(04h)=0 and GP1(04h)=2.
 *
 * Every row prints an OBS line, so a failing run is still the table. The
 * single-word commands are the control that bit 26 reads set at all, and
 * the GP0(C0h) words read back are checked against a GP0(02h) fill, so the
 * readout is known to be real. A readout left half read holds bit 26 clear
 * through a further GP0 write and still returns its data.
 */

#include <stdint.h>

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

#define READY (1u << 26)
#define POLL_CAP 20000

static int s_interruptsWereEnabled;
static uint32_t s_dmaMode;
static uint32_t s_readback[2];

/* Bit 26 polls after a word: -1 if it did not come back within POLL_CAP. */
static int waitReady(uint32_t* end) {
    for (int i = 0; i < POLL_CAP; i++) {
        uint32_t s = GPU_STATUS;
        if (s & READY) {
            *end = s;
            return i;
        }
    }
    *end = GPU_STATUS;
    return -1;
}

static void reset(void) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x04000000 | s_dmaMode;
    GPU_DATA = 0xe3000000;
    GPU_DATA = 0xe4000000 | 319 | (239 << 10);
    GPU_DATA = 0xe5000000;
    uint32_t end;
    waitReady(&end);
}

/* Writes words[], or reads GPUREAD where words[i] is READWORD, reporting
   bit 26 after each one. Returns the number of words after which bit 26
   came back. Fails the test unless that was the last word only. */
#define READWORD 0xffffffffu
static int runCommand(const char* name, const uint32_t* words, int n) {
    int rose = 0, ok = 1, r = 0;
    reset();
    for (int i = 0; i < n; i++) {
        if (words[i] == READWORD) {
            s_readback[r++ & 1] = GPU_DATA;
        } else {
            GPU_DATA = words[i];
        }
        uint32_t imm = GPU_STATUS, end;
        int polls = waitReady(&end);
        ramsyscall_printf("OBS dma=%d %s word %d/%d imm=%08x polls=%d end=%08x\n", (int)s_dmaMode, name, i + 1, n,
                          (unsigned)imm, polls, (unsigned)end);
        if (polls >= 0) rose++;
        if ((polls >= 0) != (i == n - 1)) ok = 0;
    }
    GPU_STATUS = 0x00000000;
    ramsyscall_printf("OBS dma=%d %s %s\n", (int)s_dmaMode, name, ok ? "LAST-WORD-ONLY" : "MISMATCH");
    return ok;
}

/* A GP0(C0h) readout left half read, then a GP0(E1h) written: does the
   readout still hold bit 26, and does the next GPUREAD still return data? */
static int partialReadout(void) {
    const uint32_t f[] = {0x02f8f8f8, (100 << 16) | 16, (2 << 16) | 16};
    reset();
    for (int i = 0; i < 3; i++) GPU_DATA = f[i];
    uint32_t end;
    waitReady(&end);
    GPU_DATA = 0xc0000000;
    GPU_DATA = (100 << 16) | 16;
    GPU_DATA = (2 << 16) | 2;
    for (int i = 0; i < 100000 && !(GPU_STATUS & (1u << 27)); i++);
    uint32_t d0 = GPU_DATA;
    int p0 = waitReady(&end);
    uint32_t e0 = end;
    GPU_DATA = 0xe1000000;
    int p1 = waitReady(&end);
    uint32_t e1 = end;
    uint32_t d1 = GPU_DATA;
    int p2 = waitReady(&end);
    ramsyscall_printf("OBS dma=%d partialC0 d0=%08x polls=%d end=%08x afterE1 polls=%d end=%08x d1=%08x polls=%d end=%08x\n",
                      (int)s_dmaMode, (unsigned)d0, p0, (unsigned)e0, p1, (unsigned)e1, (unsigned)d1, p2,
                      (unsigned)end);
    GPU_STATUS = 0x00000000;
    return p0 < 0 && p1 < 0 && d0 == 0x7fff7fff && d1 == 0x7fff7fff && p2 >= 0;
}

static int runAll(void) {
    int failures = !partialReadout();
    {
        const uint32_t w[] = {0x00000000};
        failures += !runCommand("nop00", w, 1);
    }
    {
        const uint32_t w[] = {0xe1000000};
        failures += !runCommand("texpageE1", w, 1);
    }
    {
        const uint32_t w[] = {0x02f8f8f8, (100 << 16) | 16, (2 << 16) | 16};
        failures += !runCommand("fill02", w, 3);
    }
    {
        const uint32_t w[] = {0xa0000000, (100 << 16) | 64, (2 << 16) | 2, 0x12345678, 0x9abcdef0};
        failures += !runCommand("cpu2vramA0", w, 5);
    }
    {
        /* 2x2 out of the GP0(02h) fill: two words, both 7fff7fff. */
        const uint32_t w[] = {0x02f8f8f8, (100 << 16) | 16, (2 << 16) | 16};
        reset();
        for (int i = 0; i < 3; i++) GPU_DATA = w[i];
        uint32_t end;
        waitReady(&end);
        const uint32_t r[] = {0xc0000000, (100 << 16) | 16, (2 << 16) | 2, READWORD, READWORD};
        s_readback[0] = s_readback[1] = 0;
        failures += !runCommand("vram2cpuC0", r, 5);
        ramsyscall_printf("OBS dma=%d vram2cpuC0 data %08x %08x\n", (int)s_dmaMode, (unsigned)s_readback[0],
                          (unsigned)s_readback[1]);
        if (s_readback[0] != 0x7fff7fff || s_readback[1] != 0x7fff7fff) failures++;
    }
    {
        const uint32_t w[] = {0x80000000, (100 << 16) | 16, (120 << 16) | 16, (4 << 16) | 4};
        failures += !runCommand("vram2vram80", w, 4);
    }
    {
        const uint32_t w[] = {0x20ffffff, (10 << 16) | 10, (10 << 16) | 20, (20 << 16) | 10};
        failures += !runCommand("flatTri20", w, 4);
    }
    {
        const uint32_t w[] = {0x28ffffff, (10 << 16) | 10, (10 << 16) | 20, (20 << 16) | 10, (20 << 16) | 20};
        failures += !runCommand("flatQuad28", w, 5);
    }
    {
        const uint32_t w[] = {0x30ff0000, (10 << 16) | 10, 0x0000ff00, (10 << 16) | 20, 0x000000ff, (20 << 16) | 10};
        failures += !runCommand("gouraudTri30", w, 6);
    }
    {
        const uint32_t w[] = {0x24808080, (10 << 16) | 10, 0x00000000, (10 << 16) | 20,
                              0x00000000, (20 << 16) | 10, 0x00000000};
        failures += !runCommand("texTri24", w, 7);
    }
    {
        const uint32_t w[] = {0x60ffffff, (30 << 16) | 30, (4 << 16) | 4};
        failures += !runCommand("rect60", w, 3);
    }
    {
        const uint32_t w[] = {0x68ffffff, (40 << 16) | 40};
        failures += !runCommand("dot68", w, 2);
    }
    {
        const uint32_t w[] = {0x40ffffff, (50 << 16) | 10, (60 << 16) | 30};
        failures += !runCommand("line40", w, 3);
    }
    {
        const uint32_t w[] = {0x48ffffff, (50 << 16) | 10, (60 << 16) | 30, (50 << 16) | 50, 0x55555555};
        failures += !runCommand("polyline48", w, 5);
    }
    {
        const uint32_t w[] = {0x58ff0000, (50 << 16) | 10, 0x0000ff00, (60 << 16) | 30, 0x55555555};
        failures += !runCommand("gouraudPolyline58", w, 5);
    }
    return failures;
}
)

CESTER_BEFORE_ALL(gpustat26,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    ramsyscall_printf("\n=== gpustat26: GPUSTAT bit 26 after each GP0 word ===\n");
    GPU_STATUS = 0x10000007;
    ramsyscall_printf("OBS gpuver idx7=%08x\n", (unsigned)GPU_DATA);
)

CESTER_AFTER_ALL(gpustat26,
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(readyOnlyAfterLastWordDmaOff, gpustat26,
    s_dmaMode = 0;
    cester_assert_int_eq(0, runAll());
)

CESTER_TEST(readyOnlyAfterLastWordCpuToGpu, gpustat26,
    s_dmaMode = 2;
    cester_assert_int_eq(0, runAll());
)
