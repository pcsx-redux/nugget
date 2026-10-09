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

// Does modplayer lose the tail of its sample upload?
//
// modplayer/modplayer.c's loadInternal() returns from SPUUploadInstruments
// once DMA CHCR busy clears, then resets 24 voices, does a few assignments
// and a memset of s_channelData, and only then SPUUnMute() writes
// SPUCNT = 0xc000, which leaves transfer mode. On hardware the last ~50-60
// bytes of an SPU RAM write are still draining when CHCR clears, and
// leaving transfer mode before the drain is done loses them. This test runs
// that exact sequence (modplayer-seq.c, compiled as C with the same flags as
// modplayer.c) and reads the block back.
//
// The sentinel layout and readback are tests/spu-upload-tail's: the block is
// written into a window pre-filled with a filler byte by a write known to
// land, read back with 256 bytes of over-read, the filler on both sides is
// the readback control, the last 64 bytes of the block carry a distinct
// pattern, and a separate control region is checked at start and end.
//
// Arms:
//   CONTROL     SPUInit, upload, SPUCNT = 0xc000 at once: must lose bytes
//   MODPLAYER   modplayer's sequence verbatim, untimed
//   MODPLAYER_T modplayer's sequence plus a root counter 2 read after the
//               CHCR wait and one before the SPUCNT write
//   REF         SPUInit, upload, 20000-iteration spin, SPUCNT = 0xc000
//
// wait= is root counter 2 ticks at sysclk/8 between CHCR idle and the
// SPUCNT write (x8 for CPU cycles); -1 when the arm is untimed.
//
// Exit codes: 0 done, 3 the reference arm lost data, 4 the readback control
// failed, 5 CONTROL lost nothing at least once (instrument broken).

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

extern "C" {
extern uint16_t T_stampChcrIdle;
extern uint16_t T_stampCtrl;
extern const unsigned T_sizeofChannelData;
void T_modplayerLoad(const uint8_t *patternTable, uint32_t spuAddr, const uint8_t *sampleData, uint32_t size);
void T_modplayerLoadTimed(const uint8_t *patternTable, uint32_t spuAddr, const uint8_t *sampleData, uint32_t size);
void T_controlLoad(uint32_t spuAddr, const uint8_t *sampleData, uint32_t size);
void T_referenceLoad(uint32_t spuAddr, const uint8_t *sampleData, uint32_t size);
}

namespace {

constexpr uint32_t kGuard = 0x100;
constexpr uint32_t kSlack = 0x100;
constexpr uint32_t kBase = 0x10000;
constexpr uint32_t kMaxSize = 0x1000;
constexpr uint32_t kMaxWindow = kGuard + kMaxSize + kGuard;
constexpr uint32_t kMaxRead = kMaxWindow + kSlack;
constexpr uint32_t kControlAddr = 0x60000;
constexpr uint32_t kControlSize = 0x800;
constexpr uint8_t kFiller = 0xa5;
constexpr unsigned kRepeats = 3;

constexpr uint32_t kSizes[] = {0x800, 0x1000};
constexpr unsigned kNumSizes = sizeof(kSizes) / sizeof(kSizes[0]);

enum ArmKind { CONTROL, MODPLAYER, MODPLAYER_T, REF };
constexpr const char *kArmNames[] = {"CONTROL", "MODPLAYER", "MODPLAYER_T", "REF"};
constexpr unsigned kNumArms = 4;

uint8_t pattern(unsigned seed, uint32_t i, uint32_t size) {
    if (i + 64 >= size) return uint8_t(0x80 | ((seed * 0x1d) ^ (i * 3)));
    return uint8_t((seed * 0x35) ^ (i * 7) ^ (i >> 3) ^ (i >> 11));
}

alignas(4) uint8_t s_upload[kMaxSize];
alignas(4) uint8_t s_filler[kMaxRead];
alignas(4) uint8_t s_readback[kMaxRead];
alignas(4) uint8_t s_control[kControlSize];
alignas(4) uint8_t s_patternTable[128] = {0};

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

void waitLines(unsigned n) {
    uint16_t h0 = COUNTERS[1].value;
    for (unsigned i = 0; i < 0x100000; i++) {
        if (uint16_t(COUNTERS[1].value - h0) >= n) break;
    }
}

// tests/spu-upload-tail's transfer, with the reference wait built in.
void transfer(uint32_t spuAddress, void *buffer, uint32_t size, bool read) {
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
    if (!read) spin(20000);
    stopTransfer();
}

void safeWrite(uint32_t spuAddress, void *buffer, uint32_t size) {
    transfer(spuAddress, buffer, size, false);
    waitLines(4);
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
    transfer(kControlAddr, s_readback, kControlSize + kSlack, true);
    Damage d = {0, -1, -1};
    for (uint32_t i = 0; i < kControlSize; i++) {
        if (s_readback[i] != s_control[i]) {
            if (d.first < 0) d.first = i;
            d.last = i;
            d.bad++;
        }
    }
    ramsyscall_printf("READBACK-CONTROL %s: %d bad (first %d last %d) %s\n", when, d.bad, d.first, d.last,
                      d.bad ? "FAIL" : "PASS");
    return d.bad == 0;
}

class ModplayerUploadTail final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::SPU m_spu;
};

class TestScene final : public psyqo::Scene {
    void start(StartReason) override;
};

ModplayerUploadTail app;
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

void ModplayerUploadTail::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void ModplayerUploadTail::createScene() { pushScene(&scene); }

void TestScene::start(StartReason) {
    char rom[33];
    for (unsigned i = 0; i < 32; i++) rom[i] = *reinterpret_cast<volatile char *>(0xbfc7ff32 + i);
    rom[32] = 0;
    ramsyscall_printf("modplayer upload tail\nROM: %s\nSPUCNT=%04x SPUSTAT=%04x RAMCNT=%04x sizeof(s_channelData)=%d\n",
                      rom, SPU_CTRL, SPU_STATUS, SPU_RAM_DTC, T_sizeofChannelData);

    COUNTERS[2].mode = 0x0200;  // sysclk/8, free running

    for (unsigned i = 0; i < kMaxRead; i++) s_filler[i] = kFiller;
    for (unsigned i = 0; i < kControlSize; i++) s_control[i] = uint8_t(0x5c ^ (i * 11) ^ (i >> 4));
    safeWrite(kControlAddr, s_control, kControlSize);
    if (!checkControl("start")) finish(4);

    bool referenceLost = false;
    bool controlKept = false;
    bool guardsHit = false;
    unsigned seed = 1;
    for (unsigned rep = 0; rep < kRepeats; rep++) {
        for (unsigned z = 0; z < kNumSizes; z++) {
            for (unsigned a = 0; a < kNumArms; a++, seed++) {
                uint32_t size = kSizes[z];
                uint32_t window = kGuard + size + kGuard;
                uint32_t start = kBase - kGuard;

                safeWrite(start, s_filler, window);
                for (uint32_t i = 0; i < size; i++) s_upload[i] = pattern(seed, i, size);
                T_stampChcrIdle = 0;
                T_stampCtrl = 0;
                int wait = -1;
                switch (a) {
                    case CONTROL:
                        T_controlLoad(kBase, s_upload, size);
                        wait = uint16_t(T_stampCtrl - T_stampChcrIdle);
                        break;
                    case MODPLAYER:
                        T_modplayerLoad(s_patternTable, kBase, s_upload, size);
                        break;
                    case MODPLAYER_T:
                        T_modplayerLoadTimed(s_patternTable, kBase, s_upload, size);
                        wait = uint16_t(T_stampCtrl - T_stampChcrIdle);
                        break;
                    case REF:
                        T_referenceLoad(kBase, s_upload, size);
                        break;
                }
                waitLines(4);

                for (uint32_t i = 0; i < window + kSlack; i++) s_readback[i] = 0;
                transfer(start, s_readback, window + kSlack, true);

                Damage head = scan(0, kGuard, 0, true);
                Damage body = scan(kGuard, size, seed, false);
                Damage tail = scan(kGuard + size, kGuard, 0, true);

                int fromEnd = body.first < 0 ? -1 : int(size) - body.first;
                ramsyscall_printf(
                    "RES rep=%d arm=%s size=%04x wait=%d ticks bad=%d firstbad=%d fromend=%d lastbad=%d "
                    "headguard=%d tailguard=%d",
                    rep, kArmNames[a], (int)size, wait, body.bad, body.first, fromEnd, body.last, head.bad, tail.bad);
                if (body.first >= 0) {
                    ramsyscall_printf(" got=%02x want=%02x", s_readback[kGuard + body.first],
                                      pattern(seed, body.first, size));
                }
                ramsyscall_printf("\n");

                if (head.bad || tail.bad) guardsHit = true;
                if (a == REF && body.bad) referenceLost = true;
                if (a == CONTROL && body.bad == 0) controlKept = true;
            }
        }
    }

    if (!checkControl("end")) finish(4);
    if (guardsHit) ramsyscall_printf("GUARD DAMAGE SEEN\n");
    if (referenceLost) {
        ramsyscall_printf("REFERENCE ARM LOST DATA\n");
        finish(3);
    }
    if (controlKept) {
        ramsyscall_printf("CONTROL ARM LOST NOTHING AT LEAST ONCE\n");
        finish(5);
    }
    ramsyscall_printf("modplayer upload tail done\n");
    finish(0);
}

int main() { return app.run(); }
