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

/* A target that never stops on its own, for STOP while RUNNING: enables the
   VBlank IRQ (the kernel's VBlank handler acknowledges it) and spins in
   spinLoop, counting. The host's STOP halts it at the next VBlank with the
   PC inside spinLoop; CONT carries on counting. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"

volatile uint32_t g_spins;

static __attribute__((noinline, noreturn)) void spinLoop(void) {
    for (;;) g_spins++;
}

int main(void) {
    ramsyscall_printf("spinirq: VBlank on, spinning\n");
    IREG = ~IRQ_VBLANK;
    IMASK |= IRQ_VBLANK;
    spinLoop();
}
