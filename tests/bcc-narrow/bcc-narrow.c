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
 * unpredictablethings.md says 8-bit (and maybe 16-bit) reads of FFFE0130h
 * (BCC) only work from the word-aligned address. This reads BCC with lw, then
 * lb at each of the four byte offsets and lh at both halfword offsets, three
 * times each, printing before and after so a crash shows as a BEFORE line
 * with no AFTER. Nothing is written.
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

#define BCC ((volatile uint8_t *)0xfffe0130)

int main(void) {
    uint32_t v;
    ramsyscall_printf("BCCN-START\n");
    for (int rep = 0; rep < 3; rep++) {
        v = *(volatile uint32_t *)BCC;
        ramsyscall_printf("BCCN rep=%d lw +0 %08x\n", rep, v);
        for (int i = 0; i < 4; i++) {
            ramsyscall_printf("BCCN rep=%d BEFORE lb +%d\n", rep, i);
            v = BCC[i];
            ramsyscall_printf("BCCN rep=%d lb +%d %02x\n", rep, i, v);
        }
        for (int i = 0; i < 4; i += 2) {
            ramsyscall_printf("BCCN rep=%d BEFORE lh +%d\n", rep, i);
            v = *(volatile uint16_t *)(BCC + i);
            ramsyscall_printf("BCCN rep=%d lh +%d %04x\n", rep, i, v);
        }
    }
    ramsyscall_printf("BCCN-DONE\n");
    return 0;
}
