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

// GP0(0x48) flat poly-line terminator detection. See gpu-polyline.c for the
// rule and the probe method. Each test issues one poly-line whose fourth
// word is the candidate under test and asserts whether an extra segment drew,
// so the failing case names the terminator rule that diverged.

CESTER_BODY(

// Background region seeded with the sentinel. Covers every vertex below.
#define PL_BG_X 0
#define PL_BG_Y 0
#define PL_BG_W 96
#define PL_BG_H 96

// The always-drawn spine v0->v1 (horizontal, so the anchor pixel is exact).
#define PL_V0X 10
#define PL_V0Y 10
#define PL_V1X 40
#define PL_V1Y 10
#define PL_SPINE_X 25   // a pixel on the spine; lit in every attempt
#define PL_SPINE_Y 10

// The probe vertex. Reached (and drawn) only if the candidate did NOT
// terminate. Its position word ((y<<16)|x) must have a 0x00 high byte so
// that, when the candidate DID terminate and this word is read as a fresh
// GP0 command, it decodes as a NOP. y=40 -> (0x0028<<16) keeps byte 3 zero.
#define PL_PROBE_X 70
#define PL_PROBE_Y 40

// Issue one flat poly-line: white spine v0->v1, the |candidate| word, the
// on-screen probe vertex, then a guaranteed real terminator. Hang-safe in
// both outcomes (see gpu-polyline.c). Deliberately hand-rolled so the exact
// word sequence sits next to the behavior under test.
static void plAttempt(uint32_t candidate) {
    waitGPU();
    GPU_DATA = 0x48000000u | (RASTER_CMD_WHITE);
    GPU_DATA = ((uint32_t)(uint16_t)PL_V0Y << 16) | (uint32_t)(uint16_t)PL_V0X;
    GPU_DATA = ((uint32_t)(uint16_t)PL_V1Y << 16) | (uint32_t)(uint16_t)PL_V1X;
    GPU_DATA = candidate;
    GPU_DATA = ((uint32_t)(uint16_t)PL_PROBE_Y << 16) |
               (uint32_t)(uint16_t)PL_PROBE_X;
    GPU_DATA = 0x50005000u;  // guaranteed terminator for the non-terminating case
}

// Let the drawn segments raster to VRAM. A short horizontal line plus one
// diagonal completes in microseconds; this loop is a wide safety margin.
// Not a GPU-status poll: in the terminating case a dangling read-as-command
// tail can hold the "ready for command" bit low, so polling could wedge -
// GP1(0x00) below flushes that tail instead.
static void plSettle(void) {
    for (volatile int i = 0; i < 30000; i++) {
    }
}

// Reset to a known GPU state and seed the working region with the sentinel
// so post-draw readback tells drawn (white) from untouched (sentinel).
static void plSetup(void) {
    rasterReset();
    rasterClearTestRegion(PL_BG_X, PL_BG_Y, PL_BG_W, PL_BG_H);
}

// Run one candidate: setup, issue, settle, then GP1(0x00) reset to flush any
// dangling read-as-command tail before VRAM readback.
static void plRun(uint32_t candidate) {
    plSetup();
    plAttempt(candidate);
    plSettle();
    rasterReset();
}

)  // CESTER_BODY

// --------------------------------------------------------------------------
// 1. 0x50005000 terminates a two-vertex poly-line. Both nibbles equal 0x5, so
//    the list ends after v0->v1: the probe vertex is never drawn.
// --------------------------------------------------------------------------
CESTER_TEST(polyline_50005000_terminates, gpu_polyline,
    plRun(0x50005000u);
    ASSERT_PIXEL_EQ(RASTER_VRAM_WHITE, PL_SPINE_X, PL_SPINE_Y);  // spine drew
    ASSERT_PIXEL_UNTOUCHED(PL_PROBE_X, PL_PROBE_Y);              // no extra segment
)

// --------------------------------------------------------------------------
// 2. 0x55555555 also terminates. It is the terminator most code emits, and it
//    works precisely because 0x55555555 & 0xF000F000 == 0x50005000 - both the
//    bits-12-15 and bits-28-31 nibbles land on 0x5.
// --------------------------------------------------------------------------
CESTER_TEST(polyline_55555555_terminates, gpu_polyline,
    plRun(0x55555555u);
    ASSERT_PIXEL_EQ(RASTER_VRAM_WHITE, PL_SPINE_X, PL_SPINE_Y);
    ASSERT_PIXEL_UNTOUCHED(PL_PROBE_X, PL_PROBE_Y);
)

// --------------------------------------------------------------------------
// 3. 0x00500050 does NOT terminate. Neither nibble-3 nor nibble-7 is 0x5
//    (the 0x5 nibbles sit at bits 4-7 and 20-23), so the word is consumed as
//    vertex v2 = (80, 80): the extra v1->v2 and v2->probe segments draw, and
//    both the candidate's own vertex and the probe pixel come out lit.
// --------------------------------------------------------------------------
CESTER_TEST(polyline_00500050_is_a_vertex, gpu_polyline,
    plRun(0x00500050u);
    ASSERT_PIXEL_EQ(RASTER_VRAM_WHITE, PL_SPINE_X, PL_SPINE_Y);
    ASSERT_PIXEL_EQ(RASTER_VRAM_WHITE, 80, 80);                 // candidate used as v2
    ASSERT_PIXEL_EQ(RASTER_VRAM_WHITE, PL_PROBE_X, PL_PROBE_Y); // probe drew -> not terminated
)

// --------------------------------------------------------------------------
// 4. 0x50000000 does NOT terminate. Only nibble-7 (bits 28-31) is 0x5; nibble-3
//    is 0x0, so 0x50000000 & 0xF000F000 == 0x50000000 != 0x50005000. A single
//    matching nibble is insufficient, so the word is consumed as a vertex and
//    the probe pixel lights.
// --------------------------------------------------------------------------
CESTER_TEST(polyline_50000000_single_nibble_no_terminate, gpu_polyline,
    plRun(0x50000000u);
    ASSERT_PIXEL_EQ(RASTER_VRAM_WHITE, PL_SPINE_X, PL_SPINE_Y);
    ASSERT_PIXEL_EQ(RASTER_VRAM_WHITE, PL_PROBE_X, PL_PROBE_Y); // probe drew -> not terminated
)
