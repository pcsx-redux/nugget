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

// U/V boundary probes. Each test renders a 1-pixel-wide textured
// rect at the chosen UV and reads back the texel. UVs that exceed
// the depth-dependent page width are the interesting cases.

CESTER_BODY(

static void drawTex8At(uint8_t u, uint8_t v) {
    rasterReset();
    rasterClearTestRegion(0, 0, 32, 8);
    setTexpage(TEX8_TX, TEX8_TY, 1);
    setTextureWindow(0, 0, 0, 0);
    rasterTexRect(TEX_MOD_NEUTRAL, 0, 0, u, v, 1, 1, CLUT8_FIELD);
    rasterFlushPrimitive();
}

static void drawTex15At(uint8_t u, uint8_t v) {
    rasterReset();
    rasterClearTestRegion(0, 0, 32, 8);
    setTexpage(TEX15_TX, TEX15_TY, 2);
    setTextureWindow(0, 0, 0, 0);
    rasterTexRect(TEX_MOD_NEUTRAL, 0, 0, u, v, 1, 1, CLUT15_FIELD);
    rasterFlushPrimitive();
}

static void drawTex4At(uint8_t u, uint8_t v) {
    rasterReset();
    rasterClearTestRegion(0, 0, 32, 8);
    setTexpage(TEX4_TX, TEX4_TY, 0);
    setTextureWindow(0, 0, 0, 0);
    rasterTexRect(TEX_MOD_NEUTRAL, 0, 0, u, v, 1, 1, CLUT4_FIELD);
    rasterFlushPrimitive();
}

)  // CESTER_BODY

// ============================================================================
// 8-bit page (128 wide × 256 tall in texel coords). U > 127 is the
// boundary. Captured value lets us infer wrap-mod / clamp / extend.
// ============================================================================

CESTER_TEST(uv8_u0_v0, gpu_raster_phase16,
    drawTex8At(0, 0);
    /* Baseline: u=0 -> CLUT8[0]. */
    ASSERT_PIXEL_EQ(expectedClut8Color(0), 0, 0);
)

CESTER_TEST(uv8_u127_v0, gpu_raster_phase16,
    drawTex8At(127, 0);
    /* Last in-page texel for 8-bit (page width = 128). */
    ASSERT_PIXEL_EQ(expectedClut8Color(127), 0, 0);
)

CESTER_TEST(uv8_u128_v0, gpu_raster_phase16,
    drawTex8At(128, 0);
    /* First off-page texel. If hardware wraps mod 128 -> CLUT8[0].
       If hardware extends into the next VRAM region -> something
       else. */
    ASSERT_PIXEL_EQ(UV8_U128_V0, 0, 0);
)

CESTER_TEST(uv8_u200_v0, gpu_raster_phase16,
    drawTex8At(200, 0);
    /* Deep off-page. */
    ASSERT_PIXEL_EQ(UV8_U200_V0, 0, 0);
)

CESTER_TEST(uv8_u255_v0, gpu_raster_phase16,
    drawTex8At(255, 0);
    /* Last representable U. */
    ASSERT_PIXEL_EQ(UV8_U255_V0, 0, 0);
)

// ============================================================================
// 15-bit page (64 wide × 256 tall). U > 63 is the boundary.
// ============================================================================

CESTER_TEST(uv15_u0_v0, gpu_raster_phase16,
    drawTex15At(0, 0);
    /* Baseline: texel(0, 0) = vram555(0, 0, 0) = 0x0000 - transparent
       per the phase-9 finding. Sentinel preserved. */
    ASSERT_PIXEL_UNTOUCHED(0, 0);
)

CESTER_TEST(uv15_u1_v0, gpu_raster_phase16,
    drawTex15At(1, 0);
    /* texel(1, 0) = vram555(1, 0, 1) = 1 | (1<<10) = 0x0401 */
    ASSERT_PIXEL_EQ(expectedTex15Color(1, 0), 0, 0);
)

CESTER_TEST(uv15_u63_v0, gpu_raster_phase16,
    drawTex15At(63, 0);
    /* Last in-page texel for 15-bit. */
    ASSERT_PIXEL_EQ(expectedTex15Color(63 & 0x1f, 0), 0, 0);
)

CESTER_TEST(uv15_u64_v0, gpu_raster_phase16,
    drawTex15At(64, 0);
    /* First off-page. Wrap mod 64 -> texel(0, 0) = transparent? Or
       extend into next texpage? */
    ASSERT_PIXEL_EQ(UV15_U64_V0, 0, 0);
)

CESTER_TEST(uv15_u128_v0, gpu_raster_phase16,
    drawTex15At(128, 0);
    /* Two pages over. */
    ASSERT_PIXEL_EQ(UV15_U128_V0, 0, 0);
)

CESTER_TEST(uv15_u255_v0, gpu_raster_phase16,
    drawTex15At(255, 0);
    ASSERT_PIXEL_EQ(UV15_U255_V0, 0, 0);
)

// ============================================================================
// V wrap. V=255 vs V=256 (8-bit wraps mod 256 to V=0).
// Note: V is uint8_t in the GP0 command word, so V=256 isn't even
// representable - it's truncated to 0. But the rasterizer's
// per-pixel V interpolation could exceed 255 if the triangle is
// large; that's a separate concern (rect path doesn't interpolate
// the same way).
//
// For rects, V starts at the command UV and increments per row. So
// V=255 with a 2-row rect would touch V=255 (drawn) and V=256
// (which wraps via uint8 truncation back to V=0).
// ============================================================================

CESTER_TEST(uv8_u0_v255, gpu_raster_phase16,
    drawTex8At(0, 255);
    /* Last in-page row. The TEX8 fixture only uploads v=0..15;
       v=255 reads VRAM at row 255 of the TEX8 page, which is
       uninitialized fixture region (filled with whatever was
       there from prior tests or boot). Capture truth. */
    ASSERT_PIXEL_EQ(UV8_U0_V255, 0, 0);
)

CESTER_TEST(uv4_u0_v0, gpu_raster_phase16,
    drawTex4At(0, 0);
    /* 4-bit baseline. Texel u=0 -> CLUT4[0] = vram555(0, 31, 0). */
    ASSERT_PIXEL_EQ(expectedClut4Color(0), 0, 0);
)

CESTER_TEST(uv4_u15_v0, gpu_raster_phase16,
    drawTex4At(15, 0);
    /* In-pattern: CLUT4[15] = vram555(15, 16, 0). */
    ASSERT_PIXEL_EQ(expectedClut4Color(15), 0, 0);
)

CESTER_TEST(uv4_u16_v0, gpu_raster_phase16,
    drawTex4At(16, 0);
    /* Just past the fixture pattern (which spans u=0..15). The
       4-bit page is 256 texels wide; u=16 is in-page but outside
       fixture data. Reads whatever VRAM contains at that texel. */
    ASSERT_PIXEL_EQ(UV4_U16_V0, 0, 0);
)

CESTER_TEST(uv4_u255_v0, gpu_raster_phase16,
    drawTex4At(255, 0);
    /* Last representable U at 4-bit. Still within the 4-bit page
       (256 wide). Outside fixture data. */
    ASSERT_PIXEL_EQ(UV4_U255_V0, 0, 0);
)
