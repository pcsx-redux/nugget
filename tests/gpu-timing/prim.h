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

#pragma once

#include <stdint.h>

// Texture page and CLUT placement in VRAM used by the sweep.
#define TP4_X 512
#define TP8_X 640
#define TP15_X 768
#define CLUT4_Y 480
#define CLUT8_Y 481

static inline uint32_t tpageX(unsigned tex) { return tex == 4 ? TP4_X / 64 : tex == 8 ? TP8_X / 64 : TP15_X / 64; }
static inline uint32_t tdepth(unsigned tex) { return tex == 4 ? 0 : tex == 8 ? 1 : 2; }
static inline uint32_t clutWord(unsigned tex) {
    return tex == 4 ? (512 / 16) | (CLUT4_Y << 6) : tex == 8 ? (512 / 16) | (CLUT8_Y << 6) : 0;
}
static inline uint32_t abr(unsigned semi) { return semi ? semi - 1 : 0; }

static inline uint32_t vtx(unsigned x, unsigned y) { return (y << 16) | x; }
static inline uint32_t uvw(unsigned u, unsigned v) {
    if (u > 255) u = 255;
    if (v > 255) v = 255;
    return (v << 8) | u;
}

static const uint32_t s_colors[4] = {0x2050a0, 0xa05020, 0x50a020, 0x606060};
