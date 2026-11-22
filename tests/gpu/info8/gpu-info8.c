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
 * display-control-commands-gp1.md lists GP1(10h) index 08h on v2 GPUs as
 * "Unknown (Returns 00000000h) (lightgun? VRAM size set via GP1(09h)?)".
 * Before each read GPUREAD is primed with a known value through index 02h
 * (texture window, set by GP0(E2h)), so "returns nothing" (GPUREAD keeps the
 * primed value) and "returns 0" can be told apart. Index 08h and its mirror
 * 18h are read under GP1(09h) = 0, 1 and 3, then indices 06h, 07h and 09h-0Fh
 * as a control. Each line is printed twice from two separate reads.
 */

#include <stdint.h>

#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

static uint32_t info(uint32_t idx) {
    GPU_STATUS = 0x10000000 | idx;
    return GPU_DATA;
}

static uint32_t primedInfo(uint32_t prime, uint32_t idx) {
    GPU_DATA = 0xe2000000 | prime;
    (void)info(2);
    return info(idx);
}

static void row(const char *tag, uint32_t idx) {
    uint32_t a = primedInfo(0x0abcde & 0xfffff, idx);
    uint32_t b = primedInfo(0x054321 & 0xfffff, idx);
    ramsyscall_printf("GI8 %s idx=%02x primedA=%08x primedB=%08x\n", tag, idx, a, b);
}

int main(void) {
    ramsyscall_printf("GI8-START version=%08x stat=%08x\n", info(7), GPU_STATUS);
    GPU_DATA = 0xe2000000 | 0x0abcde;
    ramsyscall_printf("GI8 control idx=02 A=%08x\n", info(2));
    GPU_DATA = 0xe2000000 | 0x054321;
    ramsyscall_printf("GI8 control idx=02 B=%08x\n", info(2));
    static const uint32_t vram[] = {0, 1, 3, 0};
    for (int v = 0; v < 4; v++) {
        GPU_STATUS = 0x09000000 | vram[v];
        ramsyscall_printf("GI8 gp1_09=%x\n", vram[v]);
        row("vram", 0x08);
        row("vram", 0x18);
    }
    for (uint32_t idx = 6; idx < 16; idx++) row("ctl", idx);
    GPU_DATA = 0xe2000000;
    ramsyscall_printf("GI8-DONE\n");
    return 0;
}
