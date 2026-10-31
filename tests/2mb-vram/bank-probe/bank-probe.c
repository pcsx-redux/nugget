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

// Test 1: VRAM bank presence probe.
//
// Foundation test for the rest of the suite. Triangulates between three
// possible address-decode behaviors of GP1(0x09):
//
//   1. Real second bank: address bit 9 of Y is honored. (8, 0) and (8, 512)
//      are distinct physical pixels; sentinels written at one do not
//      interfere with the other.
//   2. Mirror: the upper-bank address bit is masked off. Writing to
//      (8, 512) actually lands at (8, 0); reading (8, 512) also reads
//      (8, 0). On retail hardware this is the documented behavior with
//      GP1(0x09) bit0 = 0.
//   3. Open bus: the upper-bank region is unmapped. Reads return garbage,
//      writes are dropped. On retail hardware this is the documented
//      behavior with GP1(0x09) bit0 = 1.
//
// A naive single-coordinate round-trip (write S to y=512, read y=512, check
// for S) cannot distinguish (1) from (2) - both round-trip the sentinel. We
// must use distinct sentinels at distinct addresses and check whether the
// address bit was actually honored.

#include "common/syscalls/syscalls.h"
#include "probe-common.h"

/* C0(1Bh), installStdIo. */
NUGGET_BIOS_THUNK(installStdIo, 0xc0, 0x1b)
NUGGET_BIOS_EXTERN void __nugget_bios_installStdIo(int mode);

#define SENT_LOWER_0 0x5a5au
#define SENT_LOWER_511 0xa5a5u
#define SENT_UPPER_512 0x1234u
#define SENT_UPPER_1023 0xfedcu
#define PROBE_X 8

typedef enum {
    DECODE_REAL,     // address bit honored, distinct upper bank
    DECODE_MIRROR,   // upper address aliases to lower
    DECODE_OPENBUS,  // upper region returns garbage / drops writes
    DECODE_BROKEN,   // lower bank itself fails - suite is meaningless
} BankDecode;

static const char* decodeName(BankDecode d) {
    switch (d) {
        case DECODE_REAL:
            return "real";
        case DECODE_MIRROR:
            return "mirror";
        case DECODE_OPENBUS:
            return "open-bus";
        case DECODE_BROKEN:
            return "broken";
    }
    return "?";
}

// One probe pass under a given GP1(0x09) value. Writes four distinct
// sentinels at four distinct Y addresses, then reads them all back and
// classifies the upper-bank decode behavior. Upper writes come AFTER
// lower writes so that mirroring (upper aliasing to lower) is visible
// as a stomped lower bank on readback.
// The 4-write/4-read body shared by every arm. The caller has already put
// the GPU into the configuration under test.
static BankDecode probeBody(const char* tag, uint32_t value, int* lowerOk) {
    writePixel(PROBE_X, 0, SENT_LOWER_0);
    writePixel(PROBE_X, 511, SENT_LOWER_511);
    writePixel(PROBE_X, 512, SENT_UPPER_512);
    writePixel(PROBE_X, 1023, SENT_UPPER_1023);

    uint16_t r0 = readPixel(PROBE_X, 0);
    uint16_t r511 = readPixel(PROBE_X, 511);
    uint16_t r512 = readPixel(PROBE_X, 512);
    uint16_t r1023 = readPixel(PROBE_X, 1023);

    PROBE_RESULT("bank-probe %s=%02x r0=%04x r511=%04x r512=%04x r1023=%04x", tag, value, r0, r511, r512, r1023);

    int lowerCleanRoundTrip = (r0 == SENT_LOWER_0) && (r511 == SENT_LOWER_511);
    int upperCleanRoundTrip = (r512 == SENT_UPPER_512) && (r1023 == SENT_UPPER_1023);
    int mirrorPattern =
        (r0 == SENT_UPPER_512) && (r511 == SENT_UPPER_1023) && (r512 == SENT_UPPER_512) && (r1023 == SENT_UPPER_1023);

    if (lowerCleanRoundTrip && upperCleanRoundTrip) {
        *lowerOk = 1;
        return DECODE_REAL;
    }
    if (mirrorPattern) {
        *lowerOk = 1;
        return DECODE_MIRROR;
    }
    if (lowerCleanRoundTrip && !upperCleanRoundTrip) {
        *lowerOk = 1;
        return DECODE_OPENBUS;
    }
    *lowerOk = 0;
    return DECODE_BROKEN;
}

static BankDecode probePass(uint32_t gp1_09_value, int* lowerOk) {
    probeReset();
    gp1_09(gp1_09_value);
    return probeBody("gate", gp1_09_value, lowerOk);
}

// GP1(20h) arm. psx-spx documents GP1(20h) as the VRAM size select of the
// v1 (208-pin) GPU, which ignores GP1(09h): 504h = 2 MB, 501h = 1 MB. On a
// v2 GPU the command is not expected to open anything. GP1(00h) does NOT
// reset GP1(09h) (psx-spx, and measured on SCPH-5501: without the explicit
// gate clear these arms inherited gate=1 and read open-bus), so clear the
// gate explicitly before GP1(20h).
static BankDecode probePass20(uint32_t gp1_20_value, int* lowerOk) {
    probeReset();
    gp1_09(0);
    sendGPUStatus(0x20000000 | (gp1_20_value & 0xffffff));
    return probeBody("gp1_20", gp1_20_value, lowerOk);
}

// GP1(10h) index 7 is the GPU version on v2 (00000002h). On v0 it returns
// nothing, leaving GPUREAD at the previous latch, so seed the latch with
// index 4 (drawing area bottom-right, set by probeReset) first: idx7 == seed
// means the index was not answered.
static void printGpuVersion(void) {
    probeReset();
    GPU_STATUS = 0x10000004;
    uint32_t seed = GPU_DATA;
    GPU_STATUS = 0x10000007;
    uint32_t idx7 = GPU_DATA;
    PROBE_RESULT("gpu-version idx7=%08x seed=%08x", idx7, seed);
}

int main(void) {
#ifdef BANK_PROBE_ATCONS
    // Dev boards with an ATCONS console (DTL-H2700): route stdout there,
    // C0(1Bh) installStdIo(1). Same call as tests/load-timings.
    __nugget_bios_installStdIo(1);
#endif
    ramsyscall_printf("\n=== 573 bank-probe ===\n");

    ProbeStats stats;
    probeStatsInit(&stats);

    printGpuVersion();

    int lowerOk0 = 0;
    int lowerOk1 = 0;
    BankDecode gate0 = probePass(0, &lowerOk0);
    BankDecode gate1 = probePass(1, &lowerOk1);
    int lowerOk504 = 0;
    int lowerOk501 = 0;
    BankDecode g20_504 = probePass20(0x504, &lowerOk504);
    BankDecode g20_501 = probePass20(0x501, &lowerOk501);

    // Reset gate to 0 before leaving so any subsequent code starts in a
    // retail-compatible state.
    gp1_09(0);

    PROBE_RESULT("bank-probe summary lower_ok=%d gate0=%s gate1=%s", lowerOk0 && lowerOk1, decodeName(gate0),
                 decodeName(gate1));
    PROBE_RESULT("bank-probe gp1_20 summary lower_ok_504=%d lower_ok_501=%d gp1_20_504=%s gp1_20_501=%s", lowerOk504,
                 lowerOk501, decodeName(g20_504), decodeName(g20_501));

    if (lowerOk0 && lowerOk1) {
        PROBE_PASS(&stats, "lower-bank round-trips under both gate polarities");
    } else {
        PROBE_FAIL(&stats, "lower-bank round-trip failed (lowerOk0=%d lowerOk1=%d)", lowerOk0, lowerOk1);
    }

    int gate0Real = (gate0 == DECODE_REAL);
    int gate1Real = (gate1 == DECODE_REAL);

    if (gate0Real || gate1Real) {
        if (gate0Real && !gate1Real) {
            PROBE_PASS(&stats, "verdict=arcade-gate0-opens-upper");
        } else if (!gate0Real && gate1Real) {
            PROBE_PASS(&stats, "verdict=arcade-gate1-opens-upper");
        } else {
            PROBE_PASS(&stats, "verdict=both-polarities-open-upper");
        }
    } else if (gate0 != DECODE_BROKEN && gate1 != DECODE_BROKEN) {
        PROBE_INFO(&stats, "verdict=retail-or-no-upper-bank gate0=%s gate1=%s", decodeName(gate0), decodeName(gate1));
    } else {
        PROBE_FAIL(&stats, "verdict=broken gate0=%s gate1=%s", decodeName(gate0), decodeName(gate1));
    }

    probeStatsSummary(&stats, "bank-probe");

    probeExit(&stats);
    return 0;
}
