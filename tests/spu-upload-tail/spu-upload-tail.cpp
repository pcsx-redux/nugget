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

// What has to happen between the end of an SPU RAM DMA write (CHCR busy
// clear) and putting SPUCNT's transfer mode back to stop, for the whole
// upload to land.
//
// For each wait arm and each size, a window of sound RAM is first filled
// with a filler byte by a write known to land (fixed spin, then a settle),
// then a pattern block is written into the middle of it using the arm under
// test, then the window is read back with 256 bytes of over-read so the tail
// of the read lands in slack nobody compares. The filler on both sides of
// the block is the readback control: it was written long before, so any
// damage there belongs to the readback or to a spill from the arm's write,
// not to a lost tail. The block's last 64 bytes carry a different pattern
// from the rest, so a tail from an earlier upload cannot pass for this one.
//
// Arms:
//   A     nothing
//   B     poll SPUSTAT bit 10 until clear (bounded)
//   C     record 2048 raw SPUSTAT reads, then stop
//   H1-4  wait for root counter 1 (hblank) to advance by N
//   Sn    volatile spin of n iterations
//   E     volatile spin of 20000 iterations, the reference
//
// Each arm's wait is timed with root counter 2 at sysclk/8 (16 bits, so it
// wraps past ~15 ms) and in hblanks with root counter 1.
//
// Exit codes: 0 done, 3 the reference arm lost data, 4 the readback control
// failed, so nothing else means anything.

#include <stdint.h>

#include "common/hardware/counters.h"
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

constexpr uint32_t kGuard = 0x100;
constexpr uint32_t kSlack = 0x100;
constexpr uint32_t kBase = 0x10000;
constexpr uint32_t kMaxSize = 0x7e40;
constexpr uint32_t kMaxWindow = kGuard + kMaxSize + kGuard;
constexpr uint32_t kMaxRead = kMaxWindow + kSlack;
constexpr uint32_t kControlAddr = 0x60000;
constexpr uint32_t kControlSize = 0x800;
constexpr uint8_t kFiller = 0xa5;
constexpr unsigned kTraceLen = 2048;
constexpr unsigned kPasses = 2;

constexpr uint32_t kSizes[] = {0x800, 0x1000, 0x7e40};
constexpr unsigned kNumSizes = sizeof(kSizes) / sizeof(kSizes[0]);

enum ArmKind { NOTHING, BIT10, TRACE, HBLANK, SPIN };
struct Arm {
    const char *name;
    ArmKind kind;
    unsigned n;
};
constexpr Arm kArms[] = {
    {"A", NOTHING, 0},  {"B", BIT10, 0},     {"C", TRACE, 0},     {"H1", HBLANK, 1},   {"H2", HBLANK, 2},
    {"H4", HBLANK, 4},  {"S100", SPIN, 100}, {"S300", SPIN, 300}, {"S1000", SPIN, 1000}, {"S4000", SPIN, 4000},
    {"E", SPIN, 20000},
};
constexpr unsigned kNumArms = sizeof(kArms) / sizeof(kArms[0]);

// Body and tail sentinel use different generators, keyed by a per-run seed.
uint8_t pattern(unsigned seed, uint32_t i, uint32_t size) {
    if (i + 64 >= size) return uint8_t(0x80 | ((seed * 0x1d) ^ (i * 3)));
    return uint8_t((seed * 0x35) ^ (i * 7) ^ (i >> 3) ^ (i >> 11));
}

alignas(4) uint8_t s_upload[kMaxSize];
alignas(4) uint8_t s_filler[kMaxRead];
alignas(4) uint8_t s_readback[kMaxRead];
alignas(4) uint8_t s_control[kControlSize];
uint16_t s_trace[kTraceLen];

void waitStatus(uint16_t mask, uint16_t value) {
    for (unsigned i = 0; i < 0x10000 && (SPU_STATUS & mask) != value; i++);
}

void stopTransfer() {
    SPU_CTRL = SPU_CTRL & ~0x0030;
    waitStatus(0x0030, 0);
}

void spin(unsigned n) {
    volatile unsigned i;
    for (i = 0; i < n; i = i + 1);
}

uint16_t ticks() { return COUNTERS[2].value; }

struct ArmResult {
    uint16_t ticks;
    uint16_t lines;
    unsigned polls;
    bool sawBusy;
};

ArmResult runArm(const Arm &arm) {
    ArmResult r = {0, 0, 0, false};
    uint16_t t0 = ticks();
    uint16_t l0 = COUNTERS[1].value;
    switch (arm.kind) {
        case NOTHING:
            break;
        case BIT10:
            for (r.polls = 0; r.polls < 0x10000; r.polls++) {
                if (SPU_STATUS & 0x0400) {
                    r.sawBusy = true;
                } else {
                    break;
                }
            }
            break;
        case TRACE:
            for (unsigned i = 0; i < kTraceLen; i++) s_trace[i] = SPU_STATUS;
            break;
        case HBLANK: {
            uint16_t h0 = COUNTERS[1].value;
            for (r.polls = 0; r.polls < 0x100000; r.polls++) {
                if (uint16_t(COUNTERS[1].value - h0) >= arm.n) break;
            }
            break;
        }
        case SPIN:
            spin(arm.n);
            break;
    }
    r.ticks = ticks() - t0;
    r.lines = COUNTERS[1].value - l0;
    return r;
}

// The write is the one from tests/psyqo-spudma with the wait between CHCR
// idle and stop swapped for the arm. The read is that test's read unchanged.
ArmResult transfer(uint32_t spuAddress, void *buffer, uint32_t size, bool read, const Arm *arm) {
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
    ArmResult r = {0, 0, 0, false};
    if (arm) r = runArm(*arm);
    stopTransfer();
    return r;
}

const Arm kReference = {"E", SPIN, 20000};

// A write that lands, followed by a settle of a few lines.
void safeWrite(uint32_t spuAddress, void *buffer, uint32_t size) {
    transfer(spuAddress, buffer, size, false, &kReference);
    const Arm settle = {"settle", HBLANK, 4};
    runArm(settle);
}

struct Damage {
    unsigned bad;
    int first;
    int last;
};

Damage scan(uint32_t offset, uint32_t size, unsigned seed, bool filler) {
    Damage d = {0, -1, -1};
    for (uint32_t i = 0; i < size; i++) {
        uint8_t want = filler ? kFiller : pattern(seed, i, size);
        if (s_readback[offset + i] != want) {
            if (d.first < 0) d.first = i;
            d.last = i;
            d.bad++;
        }
    }
    return d;
}

bool checkControl(const char *when) {
    for (unsigned i = 0; i < kControlSize + kSlack; i++) s_readback[i] = 0;
    transfer(kControlAddr, s_readback, kControlSize + kSlack, true, nullptr);
    Damage d = {0, -1, -1};
    for (uint32_t i = 0; i < kControlSize; i++) {
        if (s_readback[i] != s_control[i]) {
            if (d.first < 0) d.first = i;
            d.last = i;
            d.bad++;
        }
    }
    ramsyscall_printf("CONTROL %s: %d bad (first %d last %d) %s\n", when, d.bad, d.first, d.last,
                      d.bad ? "FAIL" : "PASS");
    return d.bad == 0;
}

void printTrace(uint32_t size) {
    ramsyscall_printf("TRACE size=%04x reads=%d (value x count):", (int)size, kTraceLen);
    unsigned i = 0;
    while (i < kTraceLen) {
        unsigned j = i;
        while (j < kTraceLen && s_trace[j] == s_trace[i]) j++;
        ramsyscall_printf(" %04x@%d x%d", s_trace[i], i, j - i);
        i = j;
    }
    ramsyscall_printf("\n");
}

class SpuUploadTail final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::SPU m_spu;
};

class TestScene final : public psyqo::Scene {
    void start(StartReason) override;
};

SpuUploadTail app;
TestScene scene;

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

[[noreturn]] void finish(int code) {
    pcsx_exit(code);
    psyqo::Kernel::fastEnterCriticalSection();
    s_handlerQueues.restore();
    register int a0 asm("$4") = code;
    asm volatile("break 4, 0\n" : : "r"(a0) : "memory");
    while (1);
}

}  // namespace

void SpuUploadTail::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void SpuUploadTail::createScene() { pushScene(&scene); }

void TestScene::start(StartReason) {
    char rom[33];
    for (unsigned i = 0; i < 32; i++) rom[i] = *reinterpret_cast<volatile char *>(0xbfc7ff32 + i);
    rom[32] = 0;
    ramsyscall_printf("SPU upload tail\nROM: %s\nSPUCNT=%04x SPUSTAT=%04x\n", rom, SPU_CTRL, SPU_STATUS);

    COUNTERS[2].mode = 0x0200;  // sysclk/8, free running

    for (unsigned i = 0; i < kMaxRead; i++) s_filler[i] = kFiller;
    for (unsigned i = 0; i < kControlSize; i++) s_control[i] = uint8_t(0x5c ^ (i * 11) ^ (i >> 4));
    safeWrite(kControlAddr, s_control, kControlSize);
    if (!checkControl("start")) finish(4);

    bool referenceLost = false;
    unsigned seed = 1;
    for (unsigned pass = 0; pass < kPasses; pass++) {
        for (unsigned a = 0; a < kNumArms; a++) {
            const Arm &arm = kArms[a];
            for (unsigned z = 0; z < kNumSizes; z++, seed++) {
                uint32_t size = kSizes[z];
                uint32_t window = kGuard + size + kGuard;
                uint32_t start = kBase - kGuard;

                safeWrite(start, s_filler, window);
                for (uint32_t i = 0; i < size; i++) s_upload[i] = pattern(seed, i, size);
                ArmResult r = transfer(kBase, s_upload, size, false, &arm);
                const Arm settle = {"settle", HBLANK, 4};
                runArm(settle);

                for (uint32_t i = 0; i < window + kSlack; i++) s_readback[i] = 0;
                transfer(start, s_readback, window + kSlack, true, nullptr);

                Damage head = scan(0, kGuard, 0, true);
                Damage body = scan(kGuard, size, seed, false);
                Damage tail = scan(kGuard + size, kGuard, 0, true);

                unsigned intact = size - body.bad;
                int fromEnd = body.first < 0 ? -1 : int(size) - body.first;
                ramsyscall_printf(
                    "RES pass=%d arm=%s size=%04x wait=%d ticks lines=%d polls=%d busy=%d intact=%d bad=%d "
                    "firstbad=%d fromend=%d lastbad=%d headguard=%d tailguard=%d",
                    pass, arm.name, (int)size, r.ticks, r.lines, r.polls, r.sawBusy, intact, body.bad, body.first, fromEnd,
                    body.last, head.bad, tail.bad);
                if (body.first >= 0) {
                    ramsyscall_printf(" got=%02x want=%02x", s_readback[kGuard + body.first],
                                      pattern(seed, body.first, size));
                }
                if (tail.first >= 0) {
                    ramsyscall_printf(" tailguard[%d]=%02x", tail.first, s_readback[kGuard + size + tail.first]);
                }
                ramsyscall_printf("\n");

                if (arm.kind == TRACE && pass == 0) printTrace(size);
                if (arm.kind == SPIN && arm.n == 20000 && (body.bad || head.bad || tail.bad)) referenceLost = true;
            }
        }
    }

    if (!checkControl("end")) finish(4);
    if (referenceLost) {
        ramsyscall_printf("REFERENCE ARM LOST DATA\n");
        finish(3);
    }
    ramsyscall_printf("SPU upload tail done\n");
    finish(0);
}

int main() { return app.run(); }
