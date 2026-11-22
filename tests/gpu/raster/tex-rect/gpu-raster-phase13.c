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

// PS1 GPU rasterizer edge-behavior characterization suite, phase 13.
//
// Textured rect / sprite (GP0(0x64) and 0x66 semi-trans) at 8-bit and
// 15-bit depths. Phase-5 covered the 4-bit CLUT path; phase-13 is the
// mechanical extension to the other two depths plus:
//
//   - Variable-size sprites: 1x1, 1xN, Nx1, NxN.
//   - U/V at texture-window edges within a single page.
//   - Mask-bit interaction (set-mask, check-mask).
//   - Semi-trans interaction at each depth - confirms the texel-bit-15
//     propagation finding from phase-12 holds for rect primitives.

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
#include "texture-fixtures.h"
#include "raster-expected-phase13.h"

CESTER_BODY(
static int s_interruptsWereEnabled;

// Re-upload the standard 15-bit texture to clear the masked variant.
// Called between test groups so a later 15-bit basic test sees
// unmasked texels again.
static void restoreTex15Standard(void) {
    uploadTex15();
}
)

CESTER_BEFORE_ALL(gpu_raster_phase13,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
    uploadAllTextureFixtures();

    ramsyscall_printf("\n=== gpu-raster-phase13: textured rect 8/15-bit ===\n");
    ramsyscall_printf("Draw area: (%d,%d) .. (%d,%d), sentinel=0x%04x\n",
                      RASTER_DRAW_AREA_X1, RASTER_DRAW_AREA_Y1,
                      RASTER_DRAW_AREA_X2 - 1, RASTER_DRAW_AREA_Y2 - 1,
                      (unsigned)RASTER_SENTINEL);
    ramsyscall_printf("Grep '^OBS' to capture hardware-truth values.\n\n");
)

CESTER_AFTER_ALL(gpu_raster_phase13,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

#include "tex-rect.c"
