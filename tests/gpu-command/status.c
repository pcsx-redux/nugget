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

// GP0 env command -> GPUSTAT reflection. See gpu-command.c for the bit map.

CESTER_BODY(

// Issue a GP0 env command word and let the GPU consume it before the read.
static void cmdSend(uint32_t word) {
    waitGPU();
    sendGPUData(word);
    waitGPU();
}

)  // CESTER_BODY

// --------------------------------------------------------------------------
// 1. GP0(E1) draw-mode fields reflect in GPUSTAT bits 0-10. Field values:
//    texpage X base = 5, texpage Y base 1 = 1, semi-transparency = 2,
//    texture depth = 1, dither = 1, draw-to-display = 1  ->  low bits 0x6D5.
// --------------------------------------------------------------------------
CESTER_TEST(command_e1_draw_mode_reflects_in_gpustat, gpu_command,
    rasterReset();
    cmdSend(0xe10006d5u);
    ASSERT_STAT_EQ(0x6d5u, 0x7ffu);
)

// --------------------------------------------------------------------------
// 2. GP0(E6) set-mask-while-drawing reflects in GPUSTAT.11 (and leaves the
//    check-mask bit 12 clear).
// --------------------------------------------------------------------------
CESTER_TEST(command_e6_set_mask_reflects_in_gpustat, gpu_command,
    rasterReset();
    cmdSend(0xe6000001u);  // set-mask on, check-mask off
    ASSERT_STAT_EQ(0x800u, 0x1800u);
)

// --------------------------------------------------------------------------
// 3. GP0(E6) check-mask-before-draw reflects in GPUSTAT.12 (and leaves the
//    set-mask bit 11 clear).
// --------------------------------------------------------------------------
CESTER_TEST(command_e6_check_mask_reflects_in_gpustat, gpu_command,
    rasterReset();
    cmdSend(0xe6000002u);  // set-mask off, check-mask on
    ASSERT_STAT_EQ(0x1000u, 0x1800u);
)
