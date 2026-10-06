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
 * Does the SPU drop 32-bit writes? spu/soundprocessingunitspu.md says it
 * "occasionally seems to miss" them. A sw to an SPU register pair is two
/*
 * iomap.md says the 1F802000h expansion region (DEV8, 8-bit) "crashes on
 * 16bit access?". This does every width of read and write there, printing
 * before and after each, under the BIOS Delay/Size value and with DEV8 set
 * to 16-bit. A crash shows as a BEFORE line with no AFTER.
 *
 * Writes go to 1F802040h/41h (the POST register on dev hardware; nothing
 * on a retail board). Reads are of 1F802000h.
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

#define DEV8 (*(volatile uint32_t *)0xbf80101c)
#define R ((volatile uint8_t *)0xbf802000)

static void pass(const char *tag) {
    uint32_t v;
    ramsyscall_printf("%s BEFORE lb\n", tag);
    v = R[0];
    ramsyscall_printf("%s AFTER lb %02x\n", tag, v);
    ramsyscall_printf("%s BEFORE lh\n", tag);
    v = *(volatile uint16_t *)R;
    ramsyscall_printf("%s AFTER lh %04x\n", tag, v);
    ramsyscall_printf("%s BEFORE lw\n", tag);
    v = *(volatile uint32_t *)R;
    ramsyscall_printf("%s AFTER lw %08x\n", tag, v);
    ramsyscall_printf("%s BEFORE sb\n", tag);
    R[0x41] = 0x5a;
    ramsyscall_printf("%s AFTER sb\n", tag);
    ramsyscall_printf("%s BEFORE sh\n", tag);
    *(volatile uint16_t *)(R + 0x40) = 0x5aa5;
    ramsyscall_printf("%s AFTER sh\n", tag);
    ramsyscall_printf("%s BEFORE sw\n", tag);
    *(volatile uint32_t *)(R + 0x40) = 0x5aa55aa5;
    ramsyscall_printf("%s AFTER sw\n", tag);
}

int main(void) {
    uint32_t dev8 = DEV8;
    ramsyscall_printf("EXP2W-START dev8=%08x\n", dev8);
    pass("bios");
    DEV8 = dev8 | 0x1000;
    pass("w16");
    DEV8 = dev8;
    ramsyscall_printf("EXP2W-DONE\n");
    return 0;
}
