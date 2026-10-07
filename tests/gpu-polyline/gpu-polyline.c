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

// PS1 GPU poly-line terminator-detection behavior suite.
//
// A targeted rewrite of AmiDog psxtest_gpu's poly-line terminator probe.
// AmiDog brute-forces a wide grid of terminator candidate words and diffs
// the result against its own software GPU model, which tells you a word was
// mis-classified but not WHICH rule broke - and worse, a candidate the GPU
// does not accept as a terminator makes it consume the following words as
// more vertices and block waiting for a real terminator, wedging the test.
// This suite instead pins the documented rule with a few clear assertions,
// each structured so both outcomes are well-formed (no wedge).
//
// The rule (psx-spx "Render Line" / docs/graphicsprocessingunitgpu.md):
// in poly-line mode the vertex list ends when a word satisfies
//   (word & 0xF000F000) == 0x50005000
// i.e. nibble 3 (bits 12-15) AND nibble 7 (bits 28-31) both equal 0x5. The
// terminator is recognized on the FIRST word of a vertex - the position
// word for a flat poly-line (GP0(0x48)), the color word for gouraud
// (GP0(0x58)). A single matching nibble is not enough. Note 0x55555555 also
// satisfies the mask, which is why the common terminator works.
//
// Method: draw white segments into a sentinel-cleared region. Each attempt
// sends a two-vertex "spine" (v0->v1, always drawn), then the candidate
// word, then a guaranteed on-screen probe vertex, then a guaranteed real
// terminator 0x50005000:
//   - If the candidate IS a terminator, the poly-line ends after v0->v1; the
//     probe word is then read as a fresh command (its high byte is 0x00, a
//     GP0 NOP) so the probe pixel stays sentinel.
//   - If the candidate is NOT a terminator, it becomes v2, the probe becomes
//     v3, the v2->v3 segment lights the probe pixel, and the trailing
//     0x50005000 terminates cleanly.
// So a lit probe pixel means "candidate did NOT terminate"; a sentinel probe
// pixel means "candidate terminated". A bounded settle loop lets the drawn
// segments raster before a GP1(0x00) reset flushes any dangling
// read-as-command tail (see plAttempt).
//
// Real hardware is ground truth; PCSX-Redux is the device-under-test. Every
// assertion also logs an OBS line (see raster-helpers.h) so a hardware run
// yields a greppable ground-truth dump regardless of pass/fail.
//
// Verified on hardware: all four tests pass on real
// silicon, and match PCSX-Redux (no soft-GPU divergence observed for the
// terminator rule).

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

CESTER_BEFORE_ALL(gpu_polyline,
    // Interrupts off so VBlank/pad IRQs do not race the VRAM transfer loops.
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
    ramsyscall_printf("\n=== gpu-polyline: GP0(0x48) terminator detection ===\n");
    ramsyscall_printf("OBS lines carry per-pixel hardware-truth values.\n\n");
)

CESTER_AFTER_ALL(gpu_polyline,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

#include "polyline.c"
