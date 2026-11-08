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

// GP0(0xA0) CPU-to-VRAM upload under the E6 mask setting. Same rules as the
// GP0(0x80) copy in copy.c: set-mask forces bit 15 on every uploaded pixel,
// and check-mask leaves destination pixels that already have bit 15 set alone.
// rasterFillRect is the GP0(0xA0) upload, so the tests below drive it with
// E6 set, and prepare destinations with E6 clear.

// clang-format off

// --------------------------------------------------------------------------
// 6. The upload honors set-mask: 0x001F lands as 0x801F, and 0x0000 lands as
//    0x8000, so an uploaded transparent CLUT entry becomes opaque black.
// --------------------------------------------------------------------------
CESTER_TEST(vram_upload_honors_set_mask, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 128, 64);
    rasterSetMaskCtrl(1, 0);                        // set-mask on
    rasterFillRect(16, 8, 8, 8, RASTER_VRAM_RED);
    rasterFillRect(32, 8, 8, 8, 0x0000);
    rasterSetMaskCtrl(0, 0);
    ASSERT_PIXEL_EQ(VM_RED_MASKED, 18, 10);
    ASSERT_PIXEL_EQ(0x8000, 34, 10);
    ASSERT_PIXEL_UNTOUCHED(48, 10);
)

// --------------------------------------------------------------------------
// 7. The upload honors check-mask: a destination pixel with bit 15 set keeps
//    its value, one with bit 15 clear takes the uploaded pixel.
// --------------------------------------------------------------------------
CESTER_TEST(vram_upload_honors_check_mask, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 128, 64);
    rasterFillRect(16, 8, 8, 8, VM_GREEN_MASKED);   // protected
    rasterFillRect(24, 8, 8, 8, RASTER_VRAM_GREEN); // not protected
    rasterSetMaskCtrl(0, 1);                        // check-mask on
    rasterFillRect(16, 8, 16, 8, RASTER_VRAM_RED);
    rasterSetMaskCtrl(0, 0);
    ASSERT_PIXEL_EQ(VM_GREEN_MASKED, 18, 10);
    ASSERT_PIXEL_EQ(RASTER_VRAM_RED, 26, 10);
)

// --------------------------------------------------------------------------
// 8. Both bits on: protected pixels keep their value, the others take the
//    uploaded pixel with bit 15 forced.
// --------------------------------------------------------------------------
CESTER_TEST(vram_upload_honors_set_and_check_mask, gpu_vram_move,
    rasterReset();
    rasterClearTestRegion(0, 0, 128, 64);
    rasterFillRect(16, 8, 8, 8, VM_GREEN_MASKED);
    rasterFillRect(24, 8, 8, 8, RASTER_VRAM_GREEN);
    rasterSetMaskCtrl(1, 1);
    rasterFillRect(16, 8, 16, 8, RASTER_VRAM_RED);
    rasterSetMaskCtrl(0, 0);
    ASSERT_PIXEL_EQ(VM_GREEN_MASKED, 18, 10);
    ASSERT_PIXEL_EQ(VM_RED_MASKED, 26, 10);
)
