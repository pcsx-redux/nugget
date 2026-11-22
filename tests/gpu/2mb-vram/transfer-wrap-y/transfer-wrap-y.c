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

// Test: transfer Y wrap vs clip at the top of the upper bank (Y=1023).
//
// The GPU increments the transfer Y per row. When a transfer starts inside
// the upper bank and runs past Y=1023, two behaviors are possible:
//   WRAP - the 10-bit Y counter wraps (1024 -> 0), so the overflow rows land
//          back at the bottom of VRAM.
//   CLIP - the rows past 1023 are dropped (or the transfer stops).
//
// pcsx-redux currently CLIPS (the VRAM transfer commands clip against the
// 1024-row physical buffer). This probe is meant to be run on a 2MB-modded
// retail console with the upper-bank gate (GP1(09h).1) open to settle which
// one the silicon actually does. It only issues writes (GP0 0xA0 upload and
// GP0 0x80 VRAM-to-VRAM copy) plus single-row readbacks, so there is no
// readback stall risk on either outcome.
//
// Cannot be tested by addressing Y>=1024 directly: GPU coordinates are 11-bit
// sign-extended, so a Y field of 1024 decodes to -1024. The wrap can only be
// reached through the GPU's internal per-row Y increment, which is exactly
// what a transfer that starts at a valid Y and runs long does.

#include "probe-common.h"

#define X_UP   200  // upload column
#define X_SRC  400  // copy source column
#define X_DST  240  // copy destination column
#define COL_W  16   // narrow, even
#define BG     0x0000u

// Per-row signatures, distinct between the upload and copy passes so a stray
// leftover from one cannot satisfy the other.
static uint16_t patUp(int k) { return (uint16_t)((k * 0x1du) ^ 0x3c5au) | 1; }
static uint16_t patCp(int k) { return (uint16_t)((k * 0x2bu) ^ 0x71e3u) | 1; }

// Start at Y=992 with 64 rows: k=0..31 -> Y 992..1023 (always fit), k=32..63
// -> Y 1024..1055 -> wrap target Y 0..31 if the counter wraps.
#define START_Y 992
#define ROWS    64
#define SPLIT   32  // 1024 - START_Y

static void inspect(ProbeStats* stats, const char* label, int16_t x, uint16_t (*pat)(int)) {
    int high_ok = 0, wrap_matches = 0;
    uint16_t buf[2];
    // High zone Y 992..1023 should carry k=0..31 under both outcomes.
    for (int k = 0; k < SPLIT; k++) {
        readStrip(x + 2, START_Y + k, 2, buf);
        if (buf[0] == pat(k)) high_ok++;
    }
    // Wrap zone Y 0..31 carries k=32..63 only if the counter wrapped.
    for (int k = 0; k < (ROWS - SPLIT); k++) {
        readStrip(x + 2, k, 2, buf);
        if (buf[0] == pat(SPLIT + k)) wrap_matches++;
    }
    PROBE_RESULT("transfer-wrap-y %s start_y=%d rows=%d high_ok=%d/%d wrap_matches=%d/%d verdict=%s",
                 label, START_Y, ROWS, high_ok, SPLIT, wrap_matches, ROWS - SPLIT,
                 wrap_matches >= (ROWS - SPLIT) / 2 ? "WRAP" : "CLIP");
    // Silicon answer (573, 2026-06-15): the per-row Y counter is 10-bit and
    // WRAPS, so the high zone round-trips in full AND every overflow row
    // reappears at the bottom of VRAM. Gate against that exact outcome.
    if (high_ok == SPLIT && wrap_matches == (ROWS - SPLIT)) {
        PROBE_PASS(stats, "%s wrapped (high=%d/%d wrap=%d/%d)", label, high_ok, SPLIT, wrap_matches,
                   ROWS - SPLIT);
    } else {
        PROBE_FAIL(stats, "%s did not match silicon WRAP (high=%d/%d wrap=%d/%d)", label, high_ok,
                   SPLIT, wrap_matches, ROWS - SPLIT);
    }
}

int main(void) {
    ramsyscall_printf("\n=== 573 transfer-wrap-y ===\n");
    probeReset();
    gp1_09(1);  // open the upper bank

    ProbeStats stats;
    probeStatsInit(&stats);

    // ---- UPLOAD (GP0 0xA0) past the top ----
    gpuFullResetWithGate(1);
    // Clear both inspection zones for this column.
    fillRectViaUpload(X_UP, 0, COL_W, SPLIT, BG);
    fillRectViaUpload(X_UP, 960, COL_W, ROWS, BG);

    waitGPU();
    GPU_DATA = 0xa0000000u;
    GPU_DATA = ((uint32_t)(uint16_t)START_Y << 16) | (uint32_t)(uint16_t)X_UP;
    GPU_DATA = ((uint32_t)(uint16_t)ROWS << 16) | (uint32_t)(uint16_t)COL_W;
    {
        int eff = copyHeightEff(ROWS);
        int idx = 0;
        for (int row = 0; row < eff; row++) {
            uint16_t v = patUp(row);
            uint32_t d = (uint32_t)v | ((uint32_t)v << 16);
            for (int i = 0; i < (COL_W >> 1); i++) {
                streamPace(idx++);
                GPU_DATA = d;
            }
        }
    }
    inspect(&stats, "upload", X_UP, patUp);

    // ---- VRAM-to-VRAM copy (GP0 0x80) dst past the top ----
    gpuFullResetWithGate(1);
    // Stage a 64-row source at a safe low Y.
    fillColumn(X_SRC, COL_W, BG);
    fillRectViaUpload(X_DST, 0, COL_W, SPLIT, BG);
    fillRectViaUpload(X_DST, 960, COL_W, ROWS, BG);
    waitGPU();
    GPU_DATA = 0xa0000000u;
    GPU_DATA = ((uint32_t)(uint16_t)100 << 16) | (uint32_t)(uint16_t)X_SRC;
    GPU_DATA = ((uint32_t)(uint16_t)ROWS << 16) | (uint32_t)(uint16_t)COL_W;
    {
        int eff = copyHeightEff(ROWS);
        int idx = 0;
        for (int row = 0; row < eff; row++) {
            uint16_t v = patCp(row);
            uint32_t d = (uint32_t)v | ((uint32_t)v << 16);
            for (int i = 0; i < (COL_W >> 1); i++) {
                streamPace(idx++);
                GPU_DATA = d;
            }
        }
    }
    // Copy source (X_SRC,100) -> dst (X_DST,992), 64 rows.
    waitGPU();
    GPU_DATA = 0x80000000u;
    GPU_DATA = ((uint32_t)(uint16_t)100 << 16) | (uint32_t)(uint16_t)X_SRC;
    GPU_DATA = ((uint32_t)(uint16_t)START_Y << 16) | (uint32_t)(uint16_t)X_DST;
    GPU_DATA = ((uint32_t)(uint16_t)ROWS << 16) | (uint32_t)(uint16_t)COL_W;
    inspect(&stats, "copy", X_DST, patCp);

    // Reset the gate so anything running after starts retail-compatible.
    gp1_09(0);

    probeStatsSummary(&stats, "transfer-wrap-y");
    probeExit(&stats);
    return 0;
}
