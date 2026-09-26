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

/* ATCONS byte channel throughput, run as a monitor target: 4096 bytes put the
   way the tty driver puts them, timed with root counter 2 on sysclk/8 in
   64-byte chunks. The word channel is measured from the host with
   `monitor-runner --bench`, since raw words here would desync its framing. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/hardware/util.h"
#include "common/syscalls/syscalls.h"

#define RCNT2_VALUE (*(volatile uint16_t *)0x1f801120)
#define RCNT2_MODE (*(volatile uint16_t *)0x1f801124)
#define CHUNK 64
#define CHUNKS 64

static void bytePut(uint8_t c) {
    while ((ATCONS_STAT & 0x08) == 0) {
    }
    ATCONS_FIFO = c;
    ATCONS_IRQ2 |= 0x10;
    flushWriteQueue();
}

static uint32_t timeBytes(void) {
    uint32_t total = 0;
    for (int c = 0; c < CHUNKS; c++) {
        uint16_t t0 = RCNT2_VALUE;
        for (int i = 0; i < CHUNK - 1; i++) bytePut('.');
        bytePut('\n');
        total += (uint16_t)(RCNT2_VALUE - t0);
    }
    return total * 8;
}

static char *putdec(char *p, uint32_t v) {
    char t[12];
    int i = 0;
    do {
        t[i++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (i) *p++ = t[--i];
    return p;
}

static char *putstr(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

static void report(const char *what, uint32_t cycles) {
    char line[96], *p = line;
    uint32_t units = CHUNK * CHUNKS;
    p = putstr(p, "linkrate ");
    p = putstr(p, what);
    p = putstr(p, ": ");
    p = putdec(p, units);
    p = putstr(p, " units, ");
    p = putdec(p, cycles);
    p = putstr(p, " cycles, ");
    p = putdec(p, cycles / units);
    p = putstr(p, " cycles/unit\n");
    *p = 0;
    syscall_puts(line);
}

int main(void) {
    RCNT2_MODE = 0x0200; /* free-running, sysclk/8 */
    uint32_t b = timeBytes();
    report("bytes", b);
    register int a0 asm("a0") = 42;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    for (;;) {
    }
    return 0;
}
