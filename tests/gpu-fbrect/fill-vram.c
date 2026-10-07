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

// GP0(0x02) FillVram quirks. See gpu-fbrect.c for the behavior list. Each
// test seeds the working region with the sentinel, issues one fast-fill,
// and asserts the resulting VRAM, so the failing quirk is named at the
// call site rather than inferred from a raw pixel diff.

CESTER_BODY(

// Sentinel-seeded background region covering every fill target below.
#define FB_BG_X 0
#define FB_BG_Y 0
#define FB_BG_W 128
#define FB_BG_H 64

// Issue a GP0(0x02) fast-fill: solid |cmdColor| rectangle at (x, y) of size
// w*h, written directly to VRAM. Deliberately hand-rolled (rather than the
// gpu.h fastFill() struct helper) so the exact three command words are
// visible next to the behavior under test.
static void fbFill(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint32_t cmdColor) {
    waitGPU();
    GPU_DATA = 0x02000000u | (cmdColor & 0x00ffffffu);
    GPU_DATA = ((uint32_t)(uint16_t)y << 16) | (uint32_t)(uint16_t)x;
    GPU_DATA = ((uint32_t)(uint16_t)h << 16) | (uint32_t)(uint16_t)w;
    waitGPU();
}

// Reset to a known GPU state and seed the working region with the sentinel
// so post-fill readback tells filled (color) from untouched (sentinel).
static void fbSetup(void) {
    rasterReset();
    rasterClearTestRegion(FB_BG_X, FB_BG_Y, FB_BG_W, FB_BG_H);
}

)  // CESTER_BODY

// --------------------------------------------------------------------------
// 1. Basic solid fill covers exactly [X, X+W) x [Y, Y+H).
// --------------------------------------------------------------------------
CESTER_TEST(fbrect_fills_solid_rect, gpu_fbrect,
    fbSetup();
    fbFill(16, 8, 32, 8, RASTER_CMD_RED);  // 16-aligned X, even W
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 16, 8);    // top-left inclusive
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 47, 15);   // bottom-right inclusive (16+32-1, 8+8-1)
    ASSERT_PIXEL_UNTOUCHED(48, 8);              // one column past the right edge
    ASSERT_PIXEL_UNTOUCHED(16, 16);             // one row past the bottom edge
)

// --------------------------------------------------------------------------
// 2. The low 4 bits of X are ignored (16-pixel X granularity). A fill
//    requested at X=20 (0x14) actually starts at X=16, so columns 16..19 -
//    which the caller did not ask to fill - come out set.
// --------------------------------------------------------------------------
CESTER_TEST(fbrect_ignores_low_4_bits_of_x, gpu_fbrect,
    fbSetup();
    fbFill(20, 8, 32, 8, RASTER_CMD_RED);
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 16, 10);   // snapped-down start
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 19, 10);   // inside the ignored nibble
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 20, 10);   // the requested start, also filled
)

// --------------------------------------------------------------------------
// 3. Fast-fill bypasses the drawing-area clip. A rasterized primitive would
//    be cut at the area edge; GP0(0x02) writes past it.
// --------------------------------------------------------------------------
CESTER_TEST(fbrect_ignores_draw_area_clip, gpu_fbrect,
    fbSetup();
    setDrawingArea(0, 0, 32, 32);  // active area x<32, y<32
    fbFill(16, 8, 32, 16, RASTER_CMD_RED);  // spans x 16..47, y 8..23
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 20, 12);   // inside the area, filled
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 40, 12);   // OUTSIDE the area, still filled
)

// --------------------------------------------------------------------------
// 4a. Fast-fill ignores set-mask: it does not force bit 15 on the pixels it
//     writes (a rasterized primitive under set-mask would).
// --------------------------------------------------------------------------
CESTER_TEST(fbrect_ignores_set_mask, gpu_fbrect,
    fbSetup();
    rasterSetMaskCtrl(1, 0);  // set-mask on
    fbFill(16, 8, 32, 8, RASTER_CMD_RED);
    rasterFlushPrimitive();
    // With set-mask honored the value would be 0x801F; fast-fill writes 0x001F.
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 20, 10);
)

// --------------------------------------------------------------------------
// 4b. Fast-fill ignores check-mask: it overwrites a mask-set (bit 15 = 1)
//     destination pixel that a rasterized primitive would have skipped.
// --------------------------------------------------------------------------
CESTER_TEST(fbrect_ignores_check_mask, gpu_fbrect,
    fbSetup();
    // Plant a mask-set green pixel with a rasterized rect under set-mask.
    rasterSetMaskCtrl(1, 0);
    rasterFlatRect(RASTER_CMD_GREEN, 16, 8, 32, 8);
    rasterFlushPrimitive();
    // Enable check-mask (rasterized prims would now skip that pixel) and
    // fast-fill red over it.
    rasterSetMaskCtrl(0, 1);
    fbFill(16, 8, 32, 8, RASTER_CMD_RED);
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 20, 10);   // overwritten -> fill ignored check-mask
)

// --------------------------------------------------------------------------
// 5. X = 0x0F (all low 4 bits set). psx-spx documents that old v0 GPUs write
//    every 2nd pixel to the wrong address here, while v2 GPUs ignore the bits
//    and fill contiguously from X=0. This asserts the clean v2 behavior; a
//    v0-hardware divergence is a separate hardware-capture item.
// --------------------------------------------------------------------------
CESTER_TEST(fbrect_x_0x0f_snaps_v2_contiguous, gpu_fbrect,
    fbSetup();
    fbFill(0x0F, 8, 32, 8, RASTER_CMD_RED);
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 0, 10);    // contiguous from x=0
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 1, 10);    // a v0 GPU would skip this column
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 31, 10);   // last filled column
    ASSERT_PIXEL_UNTOUCHED(32, 10);
)
