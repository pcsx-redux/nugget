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

#include "monitor/hosts/gpu.h"

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "openbios/main/splash.h"

static uint32_t gpuInfo(uint32_t index) {
    GPU_STATUS = 0x10000000 | index;
    return GPU_DATA;
}

static void saveGpuState(void) {
    struct GpuState *state = (struct GpuState *)GPU_STATE_ADDR;
    state->gpustat = GPU_STATUS;
    state->texWindow = gpuInfo(2);
    state->drawAreaTopLeft = gpuInfo(3);
    state->drawAreaBottomRight = gpuInfo(4);
    state->drawOffset = gpuInfo(5);
    state->gpuVersion = gpuInfo(7);
    state->version = GPU_STATE_VERSION;
    state->magic = GPU_STATE_MAGIC;
}

void getVRAM(void) {
    // Before the reset below wipes it.
    saveGpuState();
    GPU_STATUS = 0x00000000;
    waitGPU();
    // sending VRAM download command
    GPU_DATA = 0xc0000000;
    GPU_DATA = 0x00000000;
    GPU_DATA = (512 << 16) | 1024;
    while ((GPU_STATUS & 0x08000000) == 0);
    GPU_STATUS = 0x04000003;
    DPCR |= 0x00000800;
    // Loader is linking at 0x801c0000, so we have room for 1MB
    // of vram between 0x80080000 and 0x80180000, and the GPU state
    // block right after it.
    const uint32_t bs = 16;
    const uint32_t vram_size = 1024 * 512 * 2;
    DMA_CTRL[DMA_GPU].MADR = 0x80000;
    DMA_CTRL[DMA_GPU].BCR = ((vram_size / (bs * 4)) << 16) | bs;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000200;
    while (DMA_CTRL[DMA_GPU].CHCR & 0x01000000);
    // GPU is going to be reset again soon so we shouldn't really
    // care about its state from here on out.
}

/* The loaders' splash: OpenBIOS's colour bars, drawn once per vblank over a
   few frames so both interlaced fields get them. The loaders run with
   interrupts off, so this polls the VBLANK bit in I_STAT, which latches
   whatever I_MASK says. */

void drawLoaderSplash(void) {
    for (int i = 0; i < 4; i++) {
        IREG = ~IRQ_VBLANK;
        while ((IREG & IRQ_VBLANK) == 0);
        drawSplashScreen();
    }
    IREG = ~IRQ_VBLANK;
}
