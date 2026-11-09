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

// Runs psyqo's asynchronous SPU DMA through its completion interrupt, which
// nothing else in the tree does: three transfers, each started by the
// completion of the one before.
//
//   stage 0  started from start(),            callback FROM_MAIN_LOOP
//   stage 1  started from stage 0's callback, callback FROM_ISR
//   stage 2  started from stage 1's callback, callback FROM_ISR
//
// Stage 2 is queued from inside a completion handler, which is the case a
// handler that clears its state after running the callback rejects.
//
// Each callback logs its stage and whether interrupts were enabled when it
// ran (SR.IEc), so a FROM_ISR callback deferred to the main loop, or the
// reverse, is caught as well as a wrong order. Then sound RAM is read back
// and every transfer is compared, along with the filler around it and a
// control written by a known-good synchronous routine. The read is longer
// than the compared window, so whatever happens at the tail of an SPU RAM
// read lands in slack nobody looks at.
//
// After that, synchronous transfers of 64 B, 512 B, 2 KiB and 8 KiB, each
// to its own slot that the drained writer first fills with the complement of
// the pattern, so every byte not written reads back wrong:
//
//   ctrl     the drained writer, which keeps every byte: the readback control
//   b2b16    psyqo dmaWrite(..., 16), then at once another psyqo dmaWrite
//   b2bdef   the same with the default block size
//   stop     psyqo dmaWrite, then the mode put back to stop at once
//   rawstop  the drained writer without its drain, the known loss
//
// stop and rawstop are positive controls, printed but not judged: rawstop
// loses its tail on silicon and shows the readback can see a loss, stop is
// the caller-side variant. Each case runs three times and prints its loss.
//
// Exit codes: 0 pass, 1 timeout, 2 out of order or wrong context, 3 data
// mismatch, 4 the readback control failed, so the data check means nothing,
// 5 back to back data loss, 6 the back to back readback control failed.

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/pcsxhw.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/gpu.hh"
#include "psyqo/kernel.hh"
#include "psyqo/scene.hh"
#include "psyqo/spu.hh"

namespace {

constexpr unsigned kStages = 3;
constexpr uint32_t kChunk = 256;
constexpr uint32_t kBase = 0x2000;
constexpr uint32_t kStride = 0x200;
constexpr uint32_t kControl = kBase + kStages * kStride;
// Compared: the three stages, the control, and the filler between them.
constexpr uint32_t kCompared = kControl + kChunk - kBase;
// Read: the compared window plus 256 bytes of slack for the read tail.
constexpr uint32_t kRead = kCompared + 256;
constexpr uint8_t kFiller = 0xa5;
constexpr unsigned kTimeoutFrames = 180;

constexpr uint8_t pattern(unsigned seed, unsigned i) { return uint8_t((seed * 0x35) ^ (i * 7) ^ (i >> 3)); }
constexpr unsigned kControlSeed = kStages + 1;

constexpr uint32_t kB2BMax = 8192;
// Guard bytes compared past the end of each slot, inside a read that runs
// 256 bytes past it.
constexpr uint32_t kB2BGuard = 64;
constexpr uint32_t kB2BSlack = 256;
constexpr uint32_t kB2BBase = 0x10000;
constexpr uint32_t kB2BStride = 0x2400;
constexpr uint32_t kB2BDummy = 0x70000;
constexpr unsigned kB2BReps = 3;
constexpr uint8_t b2bPattern(unsigned seed, unsigned i) { return uint8_t(pattern(seed, i) ^ (i >> 8)); }

alignas(4) uint8_t s_b2bSource[kB2BMax];
alignas(4) uint8_t s_b2bFill[kB2BMax + kB2BSlack];
alignas(4) uint8_t s_b2bReadback[kB2BMax + kB2BSlack];
alignas(4) uint8_t s_b2bDummy[64];
unsigned s_b2bResult = 0;

alignas(4) uint8_t s_chunks[kStages][kChunk];
alignas(4) uint8_t s_control[kChunk];
alignas(4) uint8_t s_filler[kRead];
alignas(4) uint8_t s_readback[kRead];

struct Event {
    unsigned stage;
    bool interruptsEnabled;
};
volatile unsigned s_count = 0;
volatile Event s_events[kStages + 1];

bool interruptsEnabled() {
    uint32_t sr;
    asm volatile("mfc0 %0, $12" : "=r"(sr));
    return sr & 1;
}

void logEvent(unsigned stage) {
    unsigned n = s_count;
    if (n < kStages + 1) {
        s_events[n].stage = stage;
        s_events[n].interruptsEnabled = interruptsEnabled();
    }
    s_count = n + 1;
}

// The synchronous routine the SPU tests use on silicon: transfer mode back to
// stop before the address is latched, then the DMA. Every wait is bounded.
void waitStatus(uint16_t mask, uint16_t value) {
    for (unsigned i = 0; i < 0x10000 && (SPU_STATUS & mask) != value; i++);
}

void stopTransfer() {
    SPU_CTRL = SPU_CTRL & ~0x0030;
    waitStatus(0x0030, 0);
}

// Measured on SCPH-5501 and SCPH-1001: putting the mode back to stop as soon
// as CHCR reports the DMA done loses the last 58 bytes of a write, and
// SPUSTAT bit 10 never read as busy within 65536 polls after it, so there is
// nothing to wait on. This spin kept every byte on both.
void drainWrite() {
    volatile unsigned i;
    for (i = 0; i < 20000; i = i + 1);
}

void syncTransfer(uint32_t spuAddress, void *buffer, uint32_t size, bool read, bool drain = true) {
    uint16_t mode = read ? 0x0030 : 0x0020;
    stopTransfer();
    SPU_RAM_DTA = spuAddress >> 3;
    SPU_CTRL = (SPU_CTRL & ~0x0030) | mode;
    waitStatus(0x0030, mode);
    SBUS_DEV4_CTRL = (SBUS_DEV4_CTRL & 0xf0ffffff) | (read ? 0x22000000 : 0x20000000);
    DPCR = DPCR | (1 << 19);
    DMA_CTRL[DMA_SPU].MADR = reinterpret_cast<uintptr_t>(buffer);
    DMA_CTRL[DMA_SPU].BCR = ((size / 64) << 16) | 16;
    DMA_CTRL[DMA_SPU].CHCR = read ? 0x01000200 : 0x01000201;
    for (unsigned i = 0; i < 0x100000 && (DMA_CTRL[DMA_SPU].CHCR & 0x01000000); i++);
    if (!read && drain) drainWrite();
    stopTransfer();
}

// psyqo's startup clears the BIOS exception handler queues and installs its
// own, which unhooks a resident debugger: on the farm, Unirom's kdebug sits at
// the head of priority 0 and the break below never reached it. The queue
// heads are saved before main, so before psyqo runs, and put back at exit.
struct HandlerQueues {
    HandlerQueues() {
        m_table = *reinterpret_cast<volatile uint32_t *volatile *>(0x80000100);
        m_size = *reinterpret_cast<volatile uint32_t *>(0x80000104) / sizeof(uint32_t);
        if (m_size > 8) m_size = 8;
        for (unsigned i = 0; i < m_size; i++) m_saved[i] = m_table[i];
    }
    void restore() {
        for (unsigned i = 0; i < m_size; i++) m_table[i] = m_saved[i];
    }
    volatile uint32_t *m_table;
    unsigned m_size;
    uint32_t m_saved[8];
};
HandlerQueues s_handlerQueues;

// pcsx_exit ends the run on the emulator. On hardware it is a no-op, so
// follow it with the exit break from tests/support/runtime.c, which halts
// into the resident debugger with the code in $a0.
[[noreturn]] void finish(int code) {
    pcsx_exit(code);
    psyqo::Kernel::fastEnterCriticalSection();
    s_handlerQueues.restore();
    register int a0 asm("$4") = code;
    asm volatile("break 4, 0\n" : : "r"(a0) : "memory");
    while (1);
}

class SpuDmaTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::SPU m_spu;
};

class TestScene final : public psyqo::Scene {
    void start(StartReason) override;
    void frame() override;

    unsigned m_frames = 0;

    static void startStage(unsigned stage);
    static bool checkOrder(unsigned count);
    static unsigned compare(const char *name, uint32_t offset, uint32_t size, unsigned seed);
    static unsigned backToBack();
};

SpuDmaTest app;
TestScene scene;

}  // namespace

void SpuDmaTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void SpuDmaTest::createScene() { pushScene(&scene); }

void TestScene::startStage(unsigned stage) {
    auto dmaCallback = stage == 0 ? psyqo::DMA::FROM_MAIN_LOOP : psyqo::DMA::FROM_ISR;
    app.m_spu.dmaWrite(
        kBase + stage * kStride, s_chunks[stage], kChunk,
        [stage]() {
            logEvent(stage);
            if (stage + 1 < kStages) startStage(stage + 1);
        },
        dmaCallback);
}

void TestScene::start(StartReason) {
    ramsyscall_printf("psyqo SPU async DMA\n");

    for (unsigned s = 0; s < kStages; s++) {
        for (unsigned i = 0; i < kChunk; i++) s_chunks[s][i] = pattern(s + 1, i);
    }
    for (unsigned i = 0; i < kChunk; i++) s_control[i] = pattern(kControlSeed, i);
    for (unsigned i = 0; i < kRead; i++) s_filler[i] = kFiller;

    // The back to back cases run before the completion interrupt is armed:
    // once it is, every synchronous transfer's completion runs psyqo's DMA
    // handler between CHCR going idle and the next access, which is long
    // enough for the tail to land and hides any loss.
    s_b2bResult = backToBack();

    // Background and control first, synchronously, before the completion
    // interrupt is armed.
    syncTransfer(kBase, s_filler, kRead, false);
    syncTransfer(kControl, s_control, kChunk, false);

    app.m_spu.initAsync();
    startStage(0);
}

// Stage n has to be event n, the main loop one with interrupts enabled and
// the two ISR ones with them disabled.
bool TestScene::checkOrder(unsigned count) {
    bool ok = count <= kStages;
    if (count > kStages + 1) count = kStages + 1;
    for (unsigned i = 0; i < count; i++) {
        bool wantEnabled = i == 0;
        bool good = s_events[i].stage == i && s_events[i].interruptsEnabled == wantEnabled;
        if (!good) ok = false;
        ramsyscall_printf("event %d: stage %d from %s (want stage %d from %s)  %s\n", i, s_events[i].stage,
                          s_events[i].interruptsEnabled ? "main loop" : "ISR", i, wantEnabled ? "main loop" : "ISR",
                          good ? "PASS" : "FAIL");
    }
    return ok;
}

unsigned TestScene::compare(const char *name, uint32_t offset, uint32_t size, unsigned seed) {
    unsigned bad = 0;
    int first = -1;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t want = seed ? pattern(seed, i) : kFiller;
        if (s_readback[offset + i] != want) {
            if (first < 0) first = i;
            bad++;
        }
    }
    if (bad == 0) {
        ramsyscall_printf("%-8s %04x+%03x  PASS\n", name, (int)(kBase + offset), (int)size);
    } else {
        uint8_t want = seed ? pattern(seed, first) : kFiller;
        ramsyscall_printf("%-8s %04x+%03x  %d bytes wrong, first at +%03x: %02x, want %02x  FAIL\n", name,
                          (int)(kBase + offset), (int)size, bad, first, s_readback[offset + first], want);
    }
    return bad;
}

enum B2BKind { CTRL, B2B16, B2BDEF, STOP, RAWSTOP, KINDS };

// One case: fill the slot with the complement, write the pattern the case's
// way, read the slot and 256 bytes past it with the drained routine, count.
// Returns the bytes lost inside the slot; guard bytes changed past it are
// reported through spill.
static unsigned b2bCase(B2BKind kind, uint32_t size, uint32_t addr, unsigned seed, unsigned &spill, int &first) {
    for (uint32_t i = 0; i < size; i++) s_b2bSource[i] = b2bPattern(seed, i);
    for (uint32_t i = 0; i < size + kB2BSlack; i++) s_b2bFill[i] = uint8_t(~b2bPattern(seed, i));
    syncTransfer(addr, s_b2bFill, size + kB2BSlack, false);

    switch (kind) {
        case CTRL:
            syncTransfer(addr, s_b2bSource, size, false);
            break;
        case B2B16:
            psyqo::SPU::dmaWrite(addr, s_b2bSource, size, 16);
            psyqo::SPU::dmaWrite(kB2BDummy, s_b2bDummy, sizeof(s_b2bDummy), 16);
            break;
        case B2BDEF:
            psyqo::SPU::dmaWrite(addr, s_b2bSource, size);
            psyqo::SPU::dmaWrite(kB2BDummy, s_b2bDummy, sizeof(s_b2bDummy));
            break;
        case STOP:
            psyqo::SPU::dmaWrite(addr, s_b2bSource, size, 16);
            SPU_CTRL = SPU_CTRL & ~0x0030;
            break;
        case RAWSTOP:
            syncTransfer(addr, s_b2bSource, size, false, false);
            break;
        default:
            break;
    }

    for (uint32_t i = 0; i < size + kB2BSlack; i++) s_b2bReadback[i] = 0;
    syncTransfer(addr, s_b2bReadback, size + kB2BSlack, true);

    unsigned lost = 0;
    first = -1;
    for (uint32_t i = 0; i < size; i++) {
        if (s_b2bReadback[i] != b2bPattern(seed, i)) {
            if (first < 0) first = i;
            lost++;
        }
    }
    spill = 0;
    for (uint32_t i = size; i < size + kB2BGuard; i++) {
        if (s_b2bReadback[i] != uint8_t(~b2bPattern(seed, i))) spill++;
    }
    return lost;
}

// Prints one SPUB2B line per case and repetition. Returns 0 when every judged
// case kept every byte, 6 when the readback control lost any, 5 when a back
// to back case did.
unsigned TestScene::backToBack() {
    static const uint32_t sizes[] = {64, 512, 2048, 8192};
    static const char *const kinds[KINDS] = {"ctrl", "b2b16", "b2bdef", "stop", "rawstop"};
    static const char *const verdicts[KINDS] = {"judged", "judged", "judged", "info", "info"};
    for (unsigned i = 0; i < sizeof(s_b2bDummy); i++) s_b2bDummy[i] = 0x5a;
    bool controlBad = false, b2bBad = false;
    unsigned slot = 0;
    for (uint32_t size : sizes) {
        for (unsigned k = 0; k < KINDS; k++, slot++) {
            uint32_t addr = kB2BBase + slot * kB2BStride;
            for (unsigned rep = 0; rep < kB2BReps; rep++) {
                unsigned spill;
                int first;
                unsigned lost = b2bCase(B2BKind(k), size, addr, 0x20 + slot * 4 + rep, spill, first);
                ramsyscall_printf("SPUB2B size=%d kind=%s rep=%d lost=%d first=%d spill=%d %s\n", (int)size, kinds[k],
                                  rep, lost, first, spill, verdicts[k]);
                if (k == CTRL && (lost || spill)) controlBad = true;
                if ((k == B2B16 || k == B2BDEF) && (lost || spill)) b2bBad = true;
            }
        }
    }
    if (controlBad) return 6;
    if (b2bBad) return 5;
    return 0;
}

void TestScene::frame() {
    unsigned count = s_count;
    if (count < kStages) {
        if (++m_frames <= kTimeoutFrames) return;
        if (!checkOrder(count)) {
            ramsyscall_printf("OUT OF ORDER\n");
            finish(2);
        }
        ramsyscall_printf("TIMEOUT: %d of %d completions after %d frames\n", count, kStages, kTimeoutFrames);
        finish(1);
    }

    ramsyscall_printf("all %d completions after %d frames\n", kStages, m_frames);
    if (!checkOrder(count)) {
        ramsyscall_printf("OUT OF ORDER\n");
        finish(2);
    }

    for (unsigned i = 0; i < kRead; i++) s_readback[i] = 0;
    syncTransfer(kBase, s_readback, kRead, true);

    if (compare("control", kControl - kBase, kChunk, kControlSeed) != 0) {
        ramsyscall_printf("READBACK CONTROL FAILED\n");
        finish(4);
    }
    static const char *const names[kStages] = {"stage 0", "stage 1", "stage 2"};
    static const char *const gaps[kStages] = {"gap 0", "gap 1", "gap 2"};
    unsigned bad = 0;
    for (unsigned s = 0; s < kStages; s++) {
        bad += compare(names[s], s * kStride, kChunk, s + 1);
        bad += compare(gaps[s], s * kStride + kChunk, kStride - kChunk, 0);
    }
    if (bad != 0) {
        ramsyscall_printf("DATA MISMATCH\n");
        finish(3);
    }

    if (s_b2bResult == 6) {
        ramsyscall_printf("BACK TO BACK READBACK CONTROL FAILED\n");
        finish(6);
    }
    if (s_b2bResult == 5) {
        ramsyscall_printf("BACK TO BACK DATA LOSS\n");
        finish(5);
    }

    ramsyscall_printf("All SPU DMA checks passed\n");
    finish(0);
}

int main() { return app.run(); }
