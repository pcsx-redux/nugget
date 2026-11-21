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

// PS1 GPU interrupt-request flag - GP0(1Fh) / GP1(02h) / GPUSTAT.24.
//
// A targeted rewrite of AmiDog psxtest_gpu's IRQ category. The GPU can raise
// IRQ1 by command; this suite characterizes the request/acknowledge flag in
// GPUSTAT (the flag only - no interrupt handler is installed, interrupts stay
// masked, so this just exercises the status bit).
//
// psx-spx (docs/graphicsprocessingunitgpu.md):
//   GP0(1Fh) Interrupt Request (IRQ1)          -> sets   GPUSTAT.24  (:592)
//   GP1(02h) Acknowledge GPU Interrupt (IRQ1)  -> clears GPUSTAT.24  (:658)
//   GPUSTAT.24 Interrupt Request (0=Off, 1=IRQ)                       (:907)
// A GP1(00h) full reset also clears the flag.
//
// Real hardware is ground truth; PCSX-Redux is the device-under-test. Every
// assertion logs an OBS line for hardware-truth capture.
//
// Verified on hardware: all three tests pass on real
// silicon. The soft GPU diverges on irq_gp0_1f_sets_flag - it does not raise
// GPUSTAT.24 on GP0(1Fh). (Gotcha found on silicon: reading GPUSTAT.24
// immediately after the GP1(02h) ack catches a pre-ack value; the ack helper
// settles before readback. psx-spx's "GP1(02) resets the flag" is correct.)

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

#define GPUSTAT_IRQ  0x01000000u   // GPUSTAT.24

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

CESTER_BEFORE_ALL(gpu_irq,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
    ramsyscall_printf("\n=== gpu-irq: GP0(1Fh)/GP1(02h) IRQ flag in GPUSTAT.24 ===\n");
    ramsyscall_printf("OBS lines carry hardware-truth GPUSTAT values.\n\n");
)

CESTER_AFTER_ALL(gpu_irq,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

#include "irq.c"
