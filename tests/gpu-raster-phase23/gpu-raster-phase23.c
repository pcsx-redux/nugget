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

/* gpu-raster phase-23: oversized-primitive cull DISAMBIGUATION.
 *
 * phase-14 established the per-edge limits (|dx| <= 1023, |dy| <= 511) but
 * scored every "drop" by reading ONE anchor pixel at (5,3) and checking it
 * held the sentinel. Culled and drawn-somewhere-else are the same reading at
 * a single pixel, so the drop verdicts past the boundary are not established.
 *
 * They matter because GP0 vertex coords are 11-bit signed: the GPU keeps
 * bits 0-10 and ignores 11-15 (phase-14 ct_tri_pretrunc_bit11/bit15). So a
 * coordinate of 2047 arrives as -1 and 1025 arrives as -1023, and the edge
 * delta is computed on THOSE. Those primitives are not culled at all, they
 * render somewhere else, and A3/A4b/A5 below are where that was settled.
 *
 * Redux turned out to agree on every one of those arms - it sign-extends to
 * 11 bits when it parses the vertex - so the hypothesis that it over-culled
 * the wrapped cases was wrong. What it did NOT agree on was Q2 and O3: the
 * quad's shared diagonal was not in its edge check, and its 11-bit wrap was
 * applied to the vertex but not to the vertex-plus-draw-offset sum.
 *
 * This binary reports COVERAGE over a full 1024-pixel VRAM row per arm
 * rather than one pixel: count of non-sentinel pixels, and the leftmost and
 * rightmost x at which they occur. Culled reads as count=0. Drawn-elsewhere
 * reads as count>0 with a span that says where it went.
 *
 * Every expected value here is hardware truth, identical on SCPH-1000,
 * SCPH-5501 and SCPH-7001. Grep '^OBS' to re-capture it on a farm run.
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"
#include "raster-helpers.h"

#define SCAN_W 1024
#define BAND_Y 0
#define BAND_H 48
#define PROBE_Y 8

static uint16_t s_scan[SCAN_W];

/* GP0(E5h) with both fields masked to their real 11-bit width. The nugget
 * helper ORs the raw int16 in, so a negative X corrupts the Y field. */
static inline void setDrawingOffsetMasked(int16_t x, int16_t y) {
    sendGPUData(0xe5000000u | ((uint32_t)x & 0x7ffu) |
                (((uint32_t)y & 0x7ffu) << 11));
}

static void clearBand(void) {
    /* NOT one 1024-wide call. Hardware masks transfer/rect dimensions with
     * dim & 0x3FF, so a width of exactly 1024 becomes 0 and the fill is a
     * silent no-op - phase-14's own finding, which would have left every arm
     * below reading stale VRAM instead of the sentinel. Two 512-wide halves. */
    rasterFillRect(0, BAND_Y, 512, BAND_H, RASTER_SENTINEL);
    rasterFillRect(512, BAND_Y, 512, BAND_H, RASTER_SENTINEL);
}

/* Scan one VRAM row and report coverage. Reading in 256-pixel chunks keeps
 * the C0 data phase short enough to stay comfortably inside the FIFO. */
static int s_failures = 0;
static int s_checks = 0;

static void grade(const char* arm, int count, int minx, int maxx, int ecount,
                  int eminx, int emaxx) {
    int ok = (count == ecount) && (minx == eminx) && (maxx == emaxx);
    s_checks++;
    if (!ok) {
        s_failures++;
        ramsyscall_printf("FAIL %-22s got count=%d minx=%d maxx=%d, want count=%d minx=%d maxx=%d\n",
                          arm, count, minx, maxx, ecount, eminx, emaxx);
    }
}

static void report(const char* arm, const char* predicted, int ecount, int eminx,
                   int emaxx) {
    int count = 0;
    int minx = -1;
    int maxx = -1;
    for (int base = 0; base < SCAN_W; base += 256) {
        rasterReadStrip((int16_t)base, PROBE_Y, 256, s_scan);
        for (int i = 0; i < 256; i++) {
            if (s_scan[i] != RASTER_SENTINEL) {
                count++;
                if (minx < 0) minx = base + i;
                maxx = base + i;
            }
        }
    }
    ramsyscall_printf("OBS %-22s count=%4d minx=%5d maxx=%5d  (predicted %s)\n",
                      arm, count, minx, maxx, predicted);
    grade(arm, count, minx, maxx, ecount, eminx, emaxx);
}

/* Same coverage report over every row of the band, for arms whose shape is
 * too thin for a single row to be evidence either way. */
static void reportBand(const char* arm, const char* predicted, int ecount,
                       int eminx, int emaxx) {
    int count = 0;
    int minx = -1;
    int maxx = -1;
    int miny = -1;
    int maxy = -1;
    for (int y = BAND_Y; y < BAND_Y + BAND_H; y++) {
        for (int base = 0; base < SCAN_W; base += 256) {
            rasterReadStrip((int16_t)base, (int16_t)y, 256, s_scan);
            for (int i = 0; i < 256; i++) {
                if (s_scan[i] != RASTER_SENTINEL) {
                    int x = base + i;
                    count++;
                    if (minx < 0 || x < minx) minx = x;
                    if (x > maxx) maxx = x;
                    if (miny < 0) miny = y;
                    maxy = y;
                }
            }
        }
    }
    ramsyscall_printf(
        "OBS %-22s count=%4d minx=%5d maxx=%5d miny=%4d maxy=%4d  (predicted %s)\n",
        arm, count, minx, maxx, miny, maxy, predicted);
    grade(arm, count, minx, maxx, ecount, eminx, emaxx);
}

int main(void) {
    rasterFullReset();

    ramsyscall_printf(
        "\n=== gpu-raster-phase23: oversized cull disambiguation ===\n");
    ramsyscall_printf("sentinel=0x%04x row y=%d scan x=0..%d\n", RASTER_SENTINEL,
                      PROBE_Y, SCAN_W - 1);
    ramsyscall_printf(
        "count=0 means CULLED. count>0 means the primitive was DRAWN and\n"
        "minx/maxx say where. Arm A1 is the positive control for the whole\n"
        "instrument: if A1 reports count=0 nothing below means anything.\n\n");

    /* ---- A: triangle, per-edge dx, with and without 11-bit wrap -------- */

    /* A1 control. dx=1023 exactly, no wrap. Must draw. */
    rasterReset();
    clearBand();
    rasterFlatTri(RASTER_CMD_RED, 0, 0, 1023, 0, 0, 40);
    rasterFlushPrimitive();
    report("A1_ctl_dx1023", "DRAW span 0..1023", 819, 0, 818);

    /* A2. dx=1024 -> 11-bit signed -1024, |dx|=1024, over the limit either
     * way you compute it. The one case both models agree is a cull. */
    rasterReset();
    clearBand();
    rasterFlatTri(RASTER_CMD_RED, 0, 0, 1024, 0, 0, 40);
    rasterFlushPrimitive();
    report("A2_dx1024", "CULL", 0, -1, -1);

    /* A3. Base 600, far vertex 600+1025=1625 -> truncates to -423, so the
     * edge delta is -1023 and the triangle spans -423..600. phase-14 called
     * this a drop; Redux culls it (raw delta 1025). */
    rasterReset();
    clearBand();
    rasterFlatTri(RASTER_CMD_RED, 600, 0, 1625, 0, 600, 40);
    rasterFlushPrimitive();
    report("A3_dx1025_base600", "DRAW span 0..600", 600, 0, 599);

    /* A4. Base 600, far vertex 600+2047=2647 -> wraps to 599, edge delta -1.
     * v1 REVISION: the original third vertex was (600,40), making a triangle
     * one pixel wide whose coverage at the probe row is empty under the
     * fill rule - its count=0 was a geometry artifact, not a cull, and
     * reading it as a cull would have reproduced exactly the phase-14 defect
     * this binary exists to fix. Third vertex moved to (500,40) for area. */
    rasterReset();
    clearBand();
    rasterFlatTri(RASTER_CMD_RED, 600, 0, 2647, 0, 500, 40);
    rasterFlushPrimitive();
    report("A4_dx2047_fat", "DRAW near 500..600", 0, -1, -1);

    /* A4b. A4 is a sliver BY CONSTRUCTION and no choice of third vertex
     * fixes it: a raw dx of 2047 wraps to -1, so the two vertices end up one
     * pixel apart whatever else you do, and the shape is a wedge about one
     * pixel wide at every row. A single-row scan therefore says nothing
     * about it either way - the same single-point defect this binary exists
     * to fix, one level down. Scan the whole band instead. */
    rasterReset();
    clearBand();
    rasterFlatTri(RASTER_CMD_RED, 600, 0, 2647, 0, 500, 40);
    rasterFlushPrimitive();
    reportBand("A4b_dx2047_band", "DRAW somewhere in the band", 11, 552, 599);

    /* A5. Same shape as A4 on a line, which is the primitive Redux's
     * CheckCoordL gates with the same raw-delta arithmetic. */
    rasterReset();
    clearBand();
    rasterFlatLine(RASTER_CMD_RED, 600, PROBE_Y, 2647, PROBE_Y);
    rasterFlushPrimitive();
    report("A5_line_dx2047", "DRAW sliver near 599", 2, 599, 600);

    /* ---- Q: is the quad's split diagonal in the edge check? ------------
     * PSX quad order v0,v1,v2,v3 rasterizes as (v0,v1,v2) and (v1,v2,v3);
     * v1-v2 is the shared diagonal. Redux's checkCoord4 walks only the four
     * perimeter edges v0-v1, v1-v3, v3-v2, v2-v0 and skips the diagonal,
     * under a comment asserting that is what hardware does. Nothing has
     * measured it. */

    /* Q1 control: every edge including the diagonal well inside the limit. */
    rasterReset();
    clearBand();
    rasterFlatQuad(RASTER_CMD_GREEN, 511, 0, 900, 0, 200, 40, 511, 40);
    rasterFlushPrimitive();
    report("Q1_ctl_quad", "DRAW", 374, 449, 822);

    /* Q2: perimeter edges all in limit, diagonal v1-v2 dx = -1 - 1023 =
     * -1024, one past the limit.
     *   v0=(511,0)  v1=(1023,0)  v2=(-1,40)  v3=(511,40)
     *   v0-v1 dx=512   v1-v3 dx=-512 dy=40
     *   v3-v2 dx=-512  v2-v0 dx=512  dy=-40      all in limit
     *   v1-v2 dx=-1024                           OVER
     * Diagonal checked -> count drops sharply or to 0. Not checked -> the
     * quad draws like Q1. */
    rasterReset();
    clearBand();
    rasterFlatQuad(RASTER_CMD_GREEN, 511, 0, 1023, 0, -1, 40, 511, 40);
    rasterFlushPrimitive();
    report("Q2_quad_diag_1024", "? diagonal in check or not", 0, -1, -1);

    /* Q3. Over-limit between v0 and v3, which share no triangle: T1 is
     * (v0,v1,v2) and T2 is (v1,v2,v3), so v0-v3 is not an edge of either.
     *   v0=(-1,0) v1=(511,0) v2=(511,40) v3=(1023,40)
     *   all five real edges in limit; v0-v3 dx=1024 OVER.
     * Draws -> the check ranges over real edges only. Culled -> it ranges
     * over vertex pairs or a bounding box. */
    rasterReset();
    clearBand();
    rasterFlatQuad(RASTER_CMD_GREEN, -1, 0, 511, 0, 511, 40, 1023, 40);
    rasterFlushPrimitive();
    report("Q3_quad_v0v3_1024", "? edges only or all pairs", 512, 102, 613);

    /* ---- O: is the 11-bit truncation before or after the draw offset? --
     * This is the case homebrew actually hits: a scrolled world with a large
     * GP0(E5h) offset. Redux culls on the raw command coords and adds the
     * offset afterwards, so for Redux the offset can never change a verdict.
     *
     * O3 discriminates. Offset -1000, triangle at x=1500..1600.
     *   truncate-then-offset: 1500->-548, 1600->-448, +(-1000) = -1548..-1448
     *                         entirely off the left edge, nothing visible.
     *   offset-then-truncate: 500..600, visible.
     * The delta is 100 in both models, so no cull fires either way and the
     * arm isolates the ORDER on its own. */

    /* O1 control: no offset, small triangle at 500..600. */
    rasterReset();
    clearBand();
    setDrawingOffsetMasked(0, 0);
    rasterFlatTri(RASTER_CMD_RED, 500, 0, 600, 0, 500, 40);
    rasterFlushPrimitive();
    report("O1_ctl_nooffset", "DRAW span 500..600", 80, 500, 579);

    /* O2 control: the offset register takes and shifts left. Coords already
     * inside the 11-bit range, so no truncation question is in play. If this
     * does not land at 500..600 the O3 reading below means nothing. */
    rasterReset();
    clearBand();
    setDrawingOffsetMasked(-400, 0);
    rasterFlatTri(RASTER_CMD_RED, 900, 0, 1000, 0, 900, 40);
    rasterFlushPrimitive();
    setDrawingOffsetMasked(0, 0);
    report("O2_ctl_offset_m400", "DRAW span 500..600", 80, 500, 579);

    /* O3 discriminator. Offset -1000, triangle at x=1500..1600.
     *   truncate-then-offset: 1500->-548, 1600->-448, +(-1000) = -1548..-1448,
     *                         off the left edge, count=0.
     *   offset-then-truncate: 500..600, visible.
     * The edge delta is 100 in both models, so no cull fires either way and
     * the arm isolates the ORDER by itself. */
    rasterReset();
    clearBand();
    setDrawingOffsetMasked(-1000, 0);
    rasterFlatTri(RASTER_CMD_RED, 1500, 0, 1600, 0, 1500, 40);
    rasterFlushPrimitive();
    setDrawingOffsetMasked(0, 0);
    report("O3_off_m1000_x1500", "DRAW 500..600 iff offset precedes truncation", 80, 500, 579);

    /* ---- R: variable rectangle dimension masking -----------------------
     * psx-spx line 553 documents  Xsiz = ((Xsiz-1) AND 3FFh) + 1.
     * phase-14 measured  Xsiz = Xsiz AND 3FFh  but probed only 16/1023/
     * 1024/1025, and scored each by one anchor pixel. The two formulas
     * differ ONLY at dim congruent to 0 mod 1024, i.e. at 0, 1024 and 2048,
     * so those three arms are the whole experiment and phase-14 ran one of
     * them. Rect drawn at x=200; a coverage scan reports the true width. */

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 16, 40);
    rasterFlushPrimitive();
    report("R1_ctl_w16", "DRAW 16 wide from 200", 16, 200, 215);

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 1023, 40);
    rasterFlushPrimitive();
    report("R2_w1023", "both formulas say 1023", 824, 200, 1023);

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 1024, 40);
    rasterFlushPrimitive();
    report("R3_w1024", "doc says 1024, mask says 0", 0, -1, -1);

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 1025, 40);
    rasterFlushPrimitive();
    report("R4_w1025", "both formulas say 1", 1, 200, 200);

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 2047, 40);
    rasterFlushPrimitive();
    report("R5_w2047", "both formulas say 1023", 824, 200, 1023);

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 2048, 40);
    rasterFlushPrimitive();
    report("R6_w2048", "doc says 1024, mask says 0", 0, -1, -1);

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 0, 40);
    rasterFlushPrimitive();
    report("R7_w0", "doc says 1024, mask says 0", 0, -1, -1);

    /* Height acts on whether the probe row is covered at all. Rect at y=0,
     * probe row y=8: covered iff effective height > 8. */
    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 16, 512);
    rasterFlushPrimitive();
    report("R8_h512", "doc says 512 (covered), mask says 0 (empty)", 0, -1, -1);

    rasterReset();
    clearBand();
    rasterFlatRect(RASTER_CMD_WHITE, 200, 0, 16, 0);
    rasterFlushPrimitive();
    report("R9_h0", "doc says 1024 (covered), mask says 0 (empty)", 0, -1, -1);

    ramsyscall_printf("\n=== phase23 complete === Checks: %d | Passing: %d | Failing: %d\n",
                      s_checks, s_checks - s_failures, s_failures);
    ramsyscall_printf(s_failures == 0 ? "phase23 Synthesis: SUCCESS\n"
                                      : "phase23 Synthesis: FAILURE\n");
    return s_failures;
}
