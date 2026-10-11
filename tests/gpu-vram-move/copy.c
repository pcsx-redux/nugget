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

// GP0(0x80) VRAM-to-VRAM copy behavior. See gpu-vram-move.c for the list.

CESTER_BODY(

// VRAM pixel value with the mask bit (bit 15) set, for the mask tests.
#define VM_RED_MASKED    (RASTER_VRAM_RED   | 0x8000u)  // 0x801F
#define VM_GREEN_MASKED  (RASTER_VRAM_GREEN | 0x8000u)  // 0x83E0

// Issue a GP0(0x80) VRAM-to-VRAM copy: (w x h) region from (sx, sy) to
// (dx, dy). Coordinates and sizes are absolute pixels.
static void vramMove(int16_t sx, int16_t sy, int16_t dx, int16_t dy,
                     int16_t w, int16_t h) {
    waitGPU();
    GPU_DATA = 0x80000000u;
    GPU_DATA = ((uint32_t)(uint16_t)sy << 16) | (uint32_t)(uint16_t)sx;
    GPU_DATA = ((uint32_t)(uint16_t)dy << 16) | (uint32_t)(uint16_t)dx;
    GPU_DATA = ((uint32_t)(uint16_t)h << 16) | (uint32_t)(uint16_t)w;
    waitGPU();
}

)  // CESTER_BODY

// --------------------------------------------------------------------------
// 1. The copy moves real pixel data, not a flat color. A two-color source
//    (green left half, blue right half) must arrive intact at the dest, and
//    the source must be left unchanged.
// --------------------------------------------------------------------------
CESTER_TEST(vram_move_copies_pixel_data, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 128, 64);
    // Source at (16,8), 16x8: columns 16..23 green, 24..31 blue.
    rasterFillRect(16, 8, 8, 8, RASTER_VRAM_GREEN);
    rasterFillRect(24, 8, 8, 8, RASTER_VRAM_BLUE);
    vramMove(16, 8, 16, 40, 16, 8);  // copy down to (16,40)
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(RASTER_VRAM_GREEN, 18, 44);  // dest left half
    ASSERT_PIXEL_EQ(RASTER_VRAM_BLUE, 28, 44);   // dest right half
    ASSERT_PIXEL_EQ(RASTER_VRAM_GREEN, 18, 10);  // source unchanged
    ASSERT_PIXEL_UNTOUCHED(8, 44);               // outside the dest rect
)

// --------------------------------------------------------------------------
// 2. Copy coordinates are absolute VRAM addresses: the drawing offset (E5)
//    does not shift them (a rasterized primitive would be shifted).
// --------------------------------------------------------------------------
CESTER_TEST(vram_move_coords_are_absolute, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 256, 96);
    rasterFillRect(16, 8, 8, 8, RASTER_VRAM_RED);
    setDrawingOffset(50, 50);         // would move a primitive by (50,50)
    vramMove(16, 8, 100, 8, 8, 8);    // dest requested at absolute (100,8)
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 102, 10);   // landed at absolute (100,8)
    ASSERT_PIXEL_UNTOUCHED(152, 60);             // NOT at the offset-shifted spot
)

// --------------------------------------------------------------------------
// 3. Unlike fast-fill, the copy honors set-mask: it forces bit 15 on every
//    pixel it writes. (Compare gpu-fbrect fbrect_ignores_set_mask.)
// --------------------------------------------------------------------------
CESTER_TEST(vram_move_honors_set_mask, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 128, 64);
    rasterFillRect(16, 8, 8, 8, RASTER_VRAM_RED);  // source bit15 = 0
    rasterSetMaskCtrl(1, 0);                        // set-mask on
    vramMove(16, 8, 100, 8, 8, 8);
    rasterFlushPrimitive();
    // Copy forces bit 15 -> 0x801F, not the source's 0x001F.
    ASSERT_PIXEL_EQ(VM_RED_MASKED, 102, 10);
)

// --------------------------------------------------------------------------
// 4. Unlike fast-fill, the copy honors check-mask: a destination pixel that
//    already has bit 15 set is protected from being overwritten. (Compare
//    gpu-fbrect fbrect_ignores_check_mask, which overwrote it.)
// --------------------------------------------------------------------------
CESTER_TEST(vram_move_honors_check_mask, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 128, 64);
    rasterFillRect(16, 8, 8, 8, RASTER_VRAM_RED);       // source
    rasterFillRect(100, 8, 8, 8, VM_GREEN_MASKED);      // dest: green, bit15 = 1
    rasterSetMaskCtrl(0, 1);                            // check-mask on
    vramMove(16, 8, 100, 8, 8, 8);                      // copy red over it
    rasterFlushPrimitive();
    // Destination is mask-protected, so it keeps its green, not the red copy.
    ASSERT_PIXEL_EQ(VM_GREEN_MASKED, 102, 10);
)

// --------------------------------------------------------------------------
// 5. A copy onto its own source rectangle still runs: with set-mask on it
//    rewrites every pixel with bit 15 forced, so source == dest is not a no-op.
// --------------------------------------------------------------------------
CESTER_TEST(vram_move_in_place_honors_set_mask, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 128, 64);
    rasterFillRect(16, 8, 8, 8, RASTER_VRAM_RED);  // bit15 = 0
    rasterSetMaskCtrl(1, 0);                        // set-mask on
    vramMove(16, 8, 16, 8, 8, 8);                   // source == dest
    rasterFlushPrimitive();
    ASSERT_PIXEL_EQ(VM_RED_MASKED, 18, 10);
)
