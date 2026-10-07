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

// Test: sampling a 16-bit texture page out of the upper VRAM bank.
//
// With a 2MB bank the texpage Y selector gains a second bit (texpage word
// bit 11, the historical inert "texture disable" bit), so a texture page can
// sit at Y base 0/256/512/768. This draws a textured rectangle (GP0 0x64)
// into the lower bank that samples a 16-bit texture, and checks the drawn
// pixels match the source texels.
//
// Self-validating: phase A samples a page in the LOWER bank (ty=1, Y=256) to
// confirm the textured-draw setup itself is sound; phase B samples a page in
// the UPPER bank (ty=2, Y=512). If A passes and B passes, upper-bank texture
// sampling works. If A fails the probe setup is wrong, not the emulator.

#include "probe-common.h"

#define DEST_X   16    // destination (lower bank, away from the texture pages)
#define DEST_Y   16
#define TILE     8     // square tile, even

// A 16-bit texel pattern unique per (u,v). Bit 15 (semi-transparent flag) kept
// clear and the value kept non-zero so the texel is opaque and actually drawn
// (a 0x0000 texel is transparent and skipped).
static uint16_t texel(int u, int v) {
    return (uint16_t)((((u * 7 + v * 23) & 0x7f) << 1) | 0x4001) & 0x7fff;
}

// Upload the TILE x TILE texel pattern to VRAM page base (pageX, pageY).
static void uploadTexture(int16_t pageX, int16_t pageY) {
    waitGPU();
    GPU_DATA = 0xa0000000u;
    GPU_DATA = ((uint32_t)(uint16_t)pageY << 16) | (uint32_t)(uint16_t)pageX;
    GPU_DATA = ((uint32_t)(uint16_t)TILE << 16) | (uint32_t)(uint16_t)TILE;
    int idx = 0;
    for (int v = 0; v < TILE; v++) {
        for (int u = 0; u < TILE; u += 2) {
            uint32_t w = (uint32_t)texel(u, v) | ((uint32_t)texel(u + 1, v) << 16);
            streamPace(idx++);
            GPU_DATA = w;
        }
    }
}

// pageBit11 selects the high texpage-Y bit; tyLow is texpage bit 4.
static void onePass(ProbeStats* stats, const char* label, int tyLow, int pageBit11) {
    int pageY = ((tyLow & 1) | ((pageBit11 & 1) << 1)) * 256;
    int pageX = 0;  // tx = 0 -> page X base 0

    gpuFullResetWithGate(1);
    // Drawing area covers the destination; offset zero.
    sendGPUData(0xe3000000u);                       // top-left (0,0)
    sendGPUData(0xe4000000u | ((uint32_t)511 << 10) | 1023);  // bottom-right
    sendGPUData(0xe5000000u);                       // offset (0,0)

    fillColumn(DEST_X, TILE, 0x7fffu);              // clear dest
    uploadTexture(pageX, pageY);

    // GP0(0xE1) texpage: tx=0, ty=bit4, 16-bit depth (bits7..8 = 2), blend off,
    // and bit 11 = the upper-bank page selector under 2MB.
    uint32_t tpage = 0xe1000000u | ((uint32_t)(tyLow & 1) << 4) | ((uint32_t)2 << 7) |
                     ((uint32_t)(pageBit11 & 1) << 11);
    sendGPUData(tpage);

    // GP0(0x64) textured rectangle, neutral color (0x808080 = no modulation).
    // word2: CLUT (ignored for 16-bit) in the high half, V<<8 | U in the low.
    waitGPU();
    GPU_DATA = 0x64808080u;
    GPU_DATA = ((uint32_t)(uint16_t)DEST_Y << 16) | (uint32_t)(uint16_t)DEST_X;
    GPU_DATA = (uint32_t)0 << 16;                   // CLUT=0, U=0, V=0
    GPU_DATA = ((uint32_t)TILE << 16) | (uint32_t)TILE;

    // Read the destination tile back and compare against the source texels.
    int matches = 0, opaque = 0;
    uint16_t buf[TILE];
    for (int row = 0; row < TILE; row++) {
        readStrip(DEST_X, DEST_Y + row, TILE, buf);
        for (int u = 0; u < TILE; u++) {
            uint16_t want = texel(u, row) & 0x7fff;
            if ((buf[u] & 0x7fff) == want) matches++;
            if (buf[u] != 0x7fffu) opaque++;  // changed from the cleared bg
        }
    }
    PROBE_RESULT("texpage-upper %s pageY=%d matches=%d/%d drawn=%d/%d verdict=%s", label, pageY,
                 matches, TILE * TILE, opaque, TILE * TILE, matches == TILE * TILE ? "OK" : "MISMATCH");
    if (matches == TILE * TILE) {
        PROBE_PASS(stats, "%s pageY=%d sampled correctly", label, pageY);
    } else {
        PROBE_FAIL(stats, "%s pageY=%d matches=%d/%d", label, pageY, matches, TILE * TILE);
    }
    waitGPU();
}

int main(void) {
    ramsyscall_printf("\n=== 573 texpage-upper ===\n");
    probeReset();
    gp1_09(1);  // open the upper bank

    ProbeStats stats;
    probeStatsInit(&stats);

    onePass(&stats, "lower-bank-ty1", 1, 0);  // page Y=256, sanity (validates setup)
    onePass(&stats, "upper-bank-ty2", 0, 1);  // page Y=512, the real test
    onePass(&stats, "upper-bank-ty3", 1, 1);  // page Y=768

    PROBE_INFO(&stats, "texpage-upper sweep complete");
    probeStatsSummary(&stats, "texpage-upper");

    probeExit(&stats);
    return 0;
}
