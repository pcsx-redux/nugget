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

/* What the previous program left in the GPU, saved by getVRAM() before it
   resets the GPU, at a fixed address right after the VRAM copy so the host
   can read both back. GPUSTAT carries the display mode; the GP1(10h) words
   are the texture window (index 2), drawing area top-left (3) and
   bottom-right (4), drawing offset (5) and GPU version (7, stale GPUREAD on
   the original 160-pin GPU). The display start address (GP1(05h)) has no
   readback, so it is not here. */
#define GPU_STATE_ADDR 0x80180000
#define GPU_STATE_MAGIC 0x53555047 /* "GPUS" */
#define GPU_STATE_VERSION 1

struct GpuState {
    uint32_t magic;
    uint32_t version;
    uint32_t gpustat;
    uint32_t texWindow;
    uint32_t drawAreaTopLeft;
    uint32_t drawAreaBottomRight;
    uint32_t drawOffset;
    uint32_t gpuVersion;
};

void getVRAM(void);
void drawLoaderSplash(void);
