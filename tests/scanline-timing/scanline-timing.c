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
 * Scanline and frame length, per video mode, in CPU cycles.
 *
 * gpu/timings.md gives 3406 (PAL) and 3413 (NTSC) video cycles per scanline,
 * each "(or 3406.1 or so?)". The video clock and the CPU clock come from
 * different crystals on most boards, so a CPU-cycle count converts to video
 * cycles only to within crystal tolerance. The ratio of PAL-mode to
 * NTSC-mode line length on ONE console does not depend on either crystal,
 * and that is what decides between candidate line lengths.
 *
 * Per GP1(08h) mode, two passes of a window of NFIELDS vblank edges
 * (I_STAT bit 0): CPU cycles across the window (timer 2 on the system
 * clock, unwrapped by polling), hblanks across it (timer 1), and dotclocks
 * across it (timer 0, unwrapped by polling). Timers 0 and 1 run off the GPU
 * clock and are read until stable, per the caution in timers.md.
 * A window gives up after about 3 seconds of CPU time and reports
 * fields=57005 (0xdead) when no vblank arrives: some consoles stop the
 * video timing altogether in the other region's mode.
 * IRQs masked throughout; I_STAT still latches.
 */

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}

static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

#define NFIELDS 40

static void timersOn(void) {
    COUNTERS[0].mode = TM_CLK_EXTERNAL;
    COUNTERS[1].mode = TM_CLK_EXTERNAL;
    COUNTERS[2].mode = 0;
}

static uint16_t readHblank(void) {
    uint16_t a = COUNTERS[1].value, b;
    while ((b = COUNTERS[1].value) != a) a = b;
    return a;
}

static uint16_t readDot(void) {
    for (;;) {
        uint16_t a = COUNTERS[0].value;
        uint16_t b = COUNTERS[0].value;
        if ((uint16_t)(b - a) < 16) return b;
    }
}

struct Window {
    uint32_t cycles, lines, dots, maxGap, fields;
};

static struct Window window(void) {
    struct Window w = {0, 0, 0, 0, 0};
    IREG = ~1u;
    {
        uint16_t c0 = COUNTERS[2].value;
        uint32_t t = 0;
        while (!(IREG & 1)) {
            uint16_t c = COUNTERS[2].value;
            t += (uint16_t)(c - c0);
            c0 = c;
            if (t > 100000000) { w.fields = 0xdead; return w; }
        }
    }
    IREG = ~1u;
    uint16_t lastC = COUNTERS[2].value;
    uint16_t l0 = readHblank();
    uint16_t lastD = readDot();
    uint32_t fields = 0;
    while (fields < NFIELDS && w.cycles < 100000000) {
        uint16_t c = COUNTERS[2].value;
        uint16_t dc = c - lastC;
        lastC = c;
        w.cycles += dc;
        if (dc > w.maxGap) w.maxGap = dc;
        uint16_t d = readDot();
        w.dots += (uint16_t)(d - lastD);
        lastD = d;
        if (IREG & 1) {
            IREG = ~1u;
            fields++;
        }
    }
    w.fields = fields ? fields : 0xdead;
    w.lines = (uint16_t)(readHblank() - l0);
    return w;
}

static void runMode(const char *tag, uint32_t gp1_08) {
    GPU_STATUS = 0x08000000 | gp1_08;
    GPU_STATUS = 0x03000000; /* display on, so hblank is generated normally */
    uint32_t sr = irqDisable();
    timersOn();
    for (volatile int i = 0; i < 200000; i++);
    struct Window a = window();
    struct Window b = window();
    irqRestore(sr);
    ramsyscall_printf("MODE %-9s gp1_08=%02x fields=%d,%d | cycles=%d lines=%d dots=%d gap=%d | cycles=%d lines=%d dots=%d gap=%d\n",
                      tag, (int)gp1_08, (int)a.fields, (int)b.fields, (int)a.cycles, (int)a.lines, (int)a.dots, (int)a.maxGap,
                      (int)b.cycles, (int)b.lines, (int)b.dots, (int)b.maxGap);
}

int main(void) {
    ramsyscall_printf("SCANLINE-START\n");
    /* GP1(08h): bits 0-1 hres, bit 2 vres, bit 3 PAL, bit 5 interlace, bit 6 368 */
    runMode("PAL-320", 0x09);
    runMode("NTSC-320", 0x01);
    runMode("NTSC-256", 0x00);
    runMode("NTSC-368", 0x41);
    runMode("NTSC-512", 0x02);
    runMode("NTSC-640", 0x03);
    runMode("PAL-256", 0x08);
    runMode("PAL-368", 0x49);
    runMode("PAL-512", 0x0a);
    runMode("PAL-640", 0x0b);
    runMode("NTSC-i320", 0x25);
    runMode("PAL-i320", 0x2d);
    runMode("NTSC-320", 0x01);
    ramsyscall_printf("SCANLINE-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
