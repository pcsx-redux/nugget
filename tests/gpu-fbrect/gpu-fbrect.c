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

// PS1 GPU fast-fill - GP0(0x02) FillVram - behavior suite.
//
// A targeted rewrite of AmiDog psxtest_gpu's FBRECT category. AmiDog's
// original brute-forces a 32x64 parameter grid and diffs a 128x32 region
// against its own software GPU model, which tells you a pixel came back
// wrong but not WHICH documented behavior broke. This suite instead pins
// each documented GP0(0x02) behavior with one clear assertion, so a failure
// names the quirk that diverged.
//
// GP0(0x02) writes a solid-color rectangle straight into VRAM, bypassing
// the triangle/line rasterizer. Its quirks (psx-spx "GP0(02h) FillVram",
// docs/graphicsprocessingunitgpu.md) are what this suite characterizes:
//   - X and W operate at 16-pixel granularity: the low 4 bits of X are
//     ignored, so a fill requested at X=20 actually starts at X=16.
//   - The fill bypasses the drawing-area clip (E3/E4).
//   - The fill bypasses the mask setting (E6): it neither forces bit 15 on
//     the pixels it writes (set-mask) nor skips mask-set destination pixels
//     (check-mask).
//   - On old v0 GPUs, X with all low 4 bits set (0x0F) triggers an
//     "every 2nd pixel to the wrong address" bug; v2 GPUs fill contiguously.
//
// Real hardware is ground truth; PCSX-Redux is the device-under-test. Every
// assertion also logs an OBS line (see raster-helpers.h) so a hardware run
// yields a greppable ground-truth dump regardless of pass/fail.
//
// Verified on hardware: all six tests pass on real
// silicon. The soft GPU currently diverges on the 16-pixel X-granularity
// cases (fbrect_ignores_low_4_bits_of_x, fbrect_x_0x0f_snaps_v2_contiguous):
// it fills GP0(0x02) at the literal X instead of masking off the low 4 bits.

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
)

CESTER_BEFORE_ALL(gpu_fbrect,
    // Interrupts off so VBlank/pad IRQs do not race the VRAM transfer loops.
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
    ramsyscall_printf("\n=== gpu-fbrect: GP0(0x02) FillVram behavior ===\n");
    ramsyscall_printf("OBS lines carry per-pixel hardware-truth values.\n\n");
)

CESTER_AFTER_ALL(gpu_fbrect,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

#include "fill-vram.c"
