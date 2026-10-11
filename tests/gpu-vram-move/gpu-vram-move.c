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

// PS1 GPU VRAM-to-VRAM copy - GP0(0x80) - behavior suite.
//
// A targeted rewrite of AmiDog psxtest_gpu's VRAM MOVE category (its
// runGPUVRAMTestInner "MOVE" mode). AmiDog cross-products source/dest/size
// against all four E6 mask settings and diffs a 64x64 region against its
// software GPU model; this suite instead pins each documented GP0(0x80)
// behavior with one clear assertion.
//
// GP0(0x80) copies a rectangle from one VRAM location to another. Word layout
// (psx-spx "VRAM to VRAM blitting", docs/graphicsprocessingunitgpu.md:494):
//   1: 0x80000000
//   2: source coord      (Yyyy Xxxx)
//   3: destination coord (Yyyy Xxxx)
//   4: width + height    (Ysiz Xsiz)
// Documented behavior this suite characterizes:
//   - It moves actual pixel data (not a flat fill): a two-color source
//     pattern must arrive intact at the destination, source left untouched.
//   - Coordinates are absolute VRAM addresses - unaffected by the drawing
//     offset (E5) or drawing area (E3/E4).
//   - Unlike GP0(0x02) fast-fill, the copy IS affected by the mask setting
//     (E6): set-mask forces bit 15 on the pixels it writes, and check-mask
//     protects mask-set destination pixels from being overwritten. This is
//     the direct counterpart to gpu-fbrect's mask-bypass tests.
//   - Size 0 is treated as maximum (W=0 -> 1024, H=0 -> 512); not asserted
//     here to avoid clobbering a full VRAM row.
//
// Real hardware is ground truth; PCSX-Redux is the device-under-test. Every
// assertion also logs an OBS line for hardware-truth capture.
//
// Verified on hardware: all four tests pass on real
// silicon. The soft GPU currently diverges on the mask cases
// (vram_move_honors_set_mask, vram_move_honors_check_mask): its GP0(0x80)
// copy ignores the E6 mask setting and moves pixels raw.

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

CESTER_BEFORE_ALL(gpu_vram_move,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
    ramsyscall_printf("\n=== gpu-vram-move: GP0(0x80) VRAM-to-VRAM copy ===\n");
    ramsyscall_printf("OBS lines carry per-pixel hardware-truth values.\n\n");
)

CESTER_AFTER_ALL(gpu_vram_move,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

#include "copy.c"
