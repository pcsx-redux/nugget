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

/* Console text with nobody listening: printf once per frame, forever, while
   a bar crosses the screen and the frame count goes to TTYSTALL_COUNTER.
   Run it with `psxmon run --timeout 5`; after psxmon exits and the host drops
   RTS, the bar has to keep moving and the counter has to keep climbing. On a
   monitor whose console writes wait on CTS for good, both stop at the first
   printf after the exit. VBlank stays on so a later PING can stop it and dump
   the counter. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"

#define TTYSTALL_COUNTER (*(volatile uint32_t *)0x80100000)

/* GPUSTAT bit 26 only says the GPU takes a new command word: it stays clear
   while the GPU waits for this command's parameters, so those go straight in. */
static void fill(uint32_t bgr, unsigned x, unsigned y, unsigned w, unsigned h) {
    while ((GPU_STATUS & (1 << 26)) == 0) {
    }
    GPU_DATA = 0x02000000 | bgr;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (h << 16) | w;
}

int main(void) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x08000001; /* 320x240, NTSC, 15 bpp */
    GPU_STATUS = 0x05000000;
    GPU_STATUS = 0x06c60260;
    GPU_STATUS = 0x07040010;
    GPU_STATUS = 0x03000000;
    IREG = ~IRQ_VBLANK;
    IMASK |= IRQ_VBLANK;
    for (uint32_t n = 0;; n++) {
        TTYSTALL_COUNTER = n;
        fill(0x400000, 0, 0, 320, 240);
        fill(0x00ffff, (n * 4) % 320 & ~15u, 96, 16, 48);
        ramsyscall_printf("ttystall %d\n", (int)n);
        for (volatile uint32_t i = 0; i < 40000; i++) {
        }
    }
}
