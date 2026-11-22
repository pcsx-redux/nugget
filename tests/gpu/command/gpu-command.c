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

// PS1 GPU command status-after-write - GP0 env commands reflected in GPUSTAT.
//
// A targeted rewrite of AmiDog psxtest_gpu's COMMAND category (its
// "status error after write" check, which issues each command and diffs the
// resulting GPUSTAT against its software model). Rather than a full 32-bit
// GPUSTAT diff over every command, this suite asserts the specific documented
// bit reflections of the draw-mode/mask env commands.
//
// GPUSTAT (0x1F801814 read) mirrors the last draw-mode / mask setting
// (psx-spx "GP0(E1h) Draw Mode setting", docs/graphicsprocessingunitgpu.md:
// 379-396, and "GP0(E6h) Mask Bit Setting":465-466):
//   GP0(E1h) bits 0-3  Texture page X base   -> GPUSTAT.0-3
//            bit  4    Texture page Y base 1  -> GPUSTAT.4
//            bits 5-6  Semi-transparency mode -> GPUSTAT.5-6
//            bits 7-8  Texture colour depth   -> GPUSTAT.7-8
//            bit  9    Dither 24->15          -> GPUSTAT.9
//            bit  10   Drawing to display area-> GPUSTAT.10
//   GP0(E6h) bit  0    Set-mask-while-drawing -> GPUSTAT.11
//            bit  1    Check-mask-before-draw -> GPUSTAT.12
// (The E1 bit-11 -> GPUSTAT.15 texpage-Y-base-2 mapping is v0/v2-GPU
// dependent - v0 GPUs ignore it - so it is left to a console-specific probe
// rather than asserted in this version-independent suite.)
//
// Real hardware is ground truth; PCSX-Redux is the device-under-test. Every
// assertion logs an OBS line for hardware-truth capture.
//
// Verified on hardware: all three tests pass on real
// silicon, and the soft GPU matches - GPUSTAT reflection of E1/E6 is correct
// in Redux. This suite locks that behavior as a regression guard.

#include "common/hardware/dma.h"
#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

#include "raster-helpers.h"

CESTER_BODY(
static int s_interruptsWereEnabled;

// Assert (GPU_STATUS & mask) == expected, logging an OBS line either way.
#define ASSERT_STAT_EQ(expected, mask)                                       \
    do {                                                                     \
        uint32_t _sm = (uint32_t)(mask);                                     \
        uint32_t _sv = GPU_STATUS & _sm;                                     \
        ramsyscall_printf("OBS gpustat&0x%08x=0x%08x expect=0x%08x\n",       \
                          (unsigned)_sm, (unsigned)_sv,                      \
                          (unsigned)(expected));                             \
        cester_assert_uint_eq((unsigned)(expected), (unsigned)_sv);          \
    } while (0)
)

CESTER_BEFORE_ALL(gpu_command,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
    ramsyscall_printf("\n=== gpu-command: GP0 env commands reflected in GPUSTAT ===\n");
    ramsyscall_printf("OBS lines carry hardware-truth GPUSTAT values.\n\n");
)

CESTER_AFTER_ALL(gpu_command,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

#include "status.c"
