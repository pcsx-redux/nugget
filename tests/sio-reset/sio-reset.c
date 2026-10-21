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

/* SIO_CTRL bit 6 reset readback, both ports, graded against the silicon
   results in RESULTS.md. */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))

static void show(const char *tag, uint32_t b) {
    ramsyscall_printf("%s stat=%03x mode=%04x ctrl=%04x baud=%04x\n", tag, R32(b + 4) & 0x7ff, R16(b + 8),
                      R16(b + 10), R16(b + 14));
}

static int port(int p, uint16_t mode, uint16_t ctrl, uint16_t baud) {
    uint32_t b = p ? 0xbf801050 : 0xbf801040;
    R16(b + 8) = mode;
    R16(b + 14) = baud;
    R16(b + 10) = ctrl;
    ramsyscall_printf("SIORST port %d\n", p);
    show("SIORST before", b);
    R16(b + 10) = ctrl | 0x40;
    show("SIORST after ", b);
    /* DSR/CTS (bits 7-8) are live inputs, left out of the check */
    int ok = (R32(b + 4) & 0x27f) == 0x005 && R16(b + 8) == 0 && R16(b + 10) == 0 && R16(b + 14) == 0;
    R16(b + 10) = ctrl;
    show("SIORST rewrite", b);
    ok = ok && R16(b + 8) == 0 && R16(b + 14) == 0;
    return ok;
}

int main(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12" ::"r"(sr & ~1u));
    int ok = port(0, 0x003d, 0x1c01, 0x0088);
    ok = port(1, 0x007e, 0x0c27, 0x0012) && ok;
    R16(0xbf80104a) = 0x40;
    R16(0xbf80105a) = 0x40;
    R32(0xbf801070) = ~((1u << 7) | (1u << 8));
    __asm__ volatile("mtc0 %0, $12" ::"r"(sr));
    ramsyscall_printf(ok ? "SIORST-PASS\n" : "SIORST-FAIL\n");
    return 0;
}
