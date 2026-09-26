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

#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"
#include "gpdplayer/gpdplayer.h"

extern const uint8_t _binary_test_gpd_start[];
extern const uint8_t _binary_test_gpd_end[];

#define printf ramsyscall_printf

#if defined(GPD_DEMO_CDROM)
// A(71h), _96_init
static __attribute__((always_inline)) void cdromInit() {
    register int n asm("t1") = 0x71;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void (*)())0xa0)();
}
#endif

// The stream is played from the embedded test.gpd by default. Build
// with DEFINES=GPD_DEMO_PCDRV to read test.gpd from the host through
// pcdrv instead, or DEFINES=GPD_DEMO_CDROM to read TEST.GPD from the
// root of the CD-ROM.
static int openStream() {
#if defined(GPD_DEMO_PCDRV)
    return GPD_InitPCdrv("test.gpd");
#elif defined(GPD_DEMO_CDROM)
    // The BIOS CD-ROM code relies on interrupts. And when this
    // binary is sideloaded instead of booted from the disc, the
    // BIOS hasn't initialized the CD-ROM yet, so do it here.
    leaveCriticalSection();
    cdromInit();
    return GPD_InitCDRom("cdrom:\\TEST.GPD;1");
#else
    uint32_t size = _binary_test_gpd_end - _binary_test_gpd_start;
    return GPD_InitMemory(_binary_test_gpd_start, size);
#endif
}

static void waitVSync() {
    int wasLocked = enterCriticalSection();
    uint32_t imask = IMASK;

    IMASK = imask | IRQ_VBLANK;

    while ((IREG & IRQ_VBLANK) == 0)
        ;
    IREG &= ~IRQ_VBLANK;
    IMASK = imask;
    if (!wasLocked) leaveCriticalSection();
}

void main() {
    unsigned loops = 0;
    while (1) {
        if (!openStream()) {
            printf("Invalid GPUDUMP stream.\n");
            return;
        }
        while (GPD_PlayFrame()) waitVSync();
        printf("GPUDUMP loop %u done, GPU version %lu, %lu frames\n", loops++, GPD_GPUVersion, GPD_Frame);
    }
}
