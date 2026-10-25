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


/*
 * GPUSTAT bits 0-10 after a textured polygon. GP0(E1h) loads all eleven; a
 * textured polygon's texpage loads bits 0-8 only, and bits 9-10 of the
 * polygon's texpage are ignored, so dither and drawing to the display area
 * keep their GP0(E1h) values. Measured on a v0 (SCPH-1000) and a v2
 * (SCPH-1001) GPU.
 */

#include <stdint.h>

#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
static int s_interruptsWereEnabled;

static uint32_t stat(void) {
    while ((GPU_STATUS & 0x14000000) != 0x14000000);
    return GPU_STATUS & 0x7ff;
}

static void setup(uint32_t e1) {
    GPU_STATUS = 0x00000000;
    GPU_DATA = 0xe3000000;
    GPU_DATA = 0xe4000000 | 319 | (239 << 10);
    GPU_DATA = 0xe5000000;
    GPU_DATA = 0xe1000000 | e1;
}

static void texTri(uint16_t tp) {
    GPU_DATA = 0x24808080;
    GPU_DATA = (10 << 16) | 10;
    GPU_DATA = 0x00000000;
    GPU_DATA = (10 << 16) | 20;
    GPU_DATA = (uint32_t)tp << 16;
    GPU_DATA = (20 << 16) | 10;
    GPU_DATA = 0x00000000;
}

static void flatTri(void) {
    GPU_DATA = 0x20ffffff;
    GPU_DATA = (10 << 16) | 10;
    GPU_DATA = (10 << 16) | 20;
    GPU_DATA = (20 << 16) | 10;
}
)

CESTER_BEFORE_ALL(gpustat10,
    s_interruptsWereEnabled = enterCriticalSection();
)

CESTER_AFTER_ALL(gpustat10,
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(e1LoadsBits0To10, gpustat10,
    setup(0x400);
    cester_assert_uint_eq(0x400, stat());
    setup(0x600);
    cester_assert_uint_eq(0x600, stat());
)

CESTER_TEST(texturedPolyKeepsDfe, gpustat10,
    setup(0x400);
    texTri(0x0005);
    cester_assert_uint_eq(0x405, stat());
    flatTri();
    cester_assert_uint_eq(0x405, stat());
)

CESTER_TEST(texturedPolyKeepsDither, gpustat10,
    setup(0x600);
    texTri(0x0005);
    cester_assert_uint_eq(0x605, stat());
)

CESTER_TEST(texturedPolyIgnoresTexpageBits9And10, gpustat10,
    setup(0x400);
    texTri(0x0605);
    cester_assert_uint_eq(0x405, stat());
    setup(0x000);
    texTri(0x0605);
    cester_assert_uint_eq(0x005, stat());
)
