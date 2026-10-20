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

// GPU::reinitialize() after initialize(), read back through GPUSTAT:
//   bit 23     display disabled (GP1(03h).0)
//   bit 20     video mode, 1 = PAL (GP1(08h).3)
//   bits 17-18 horizontal resolution (GP1(08h).0-1)
//   bits 0-10  texpage attributes, zeroed by the GP1(00h) reset
//
// The mode bits prove each reinitialize() reached the GPU, so a display bit
// reading 0 is not the GPU having ignored the call. The last arm calls
// reinitialize() while a chain is still transferring. The chain ends with a
// small fill, read back from VRAM afterwards, which the reset would discard
// if it was still queued, then a node setting the texpage to a marker, which
// GPUSTAT shows afterwards if it reached the GPU after the reset.

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/hardware/gpu.hh"
#include "psyqo/kernel.hh"
#include "psyqo/scene.hh"

namespace {

constexpr uint32_t kMark = 0x555;
constexpr uint32_t kProbeX = 992;
constexpr uint32_t kProbeY = 500;

template <size_t N>
struct RawFragment : public psyqo::Fragments::ChainEntry {
    constexpr size_t getActualFragmentSize() const { return N; }
    uint32_t words[N];
};

class ReinitTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class TestScene final : public psyqo::Scene {
    void frame() override;

    bool m_done = false;
    unsigned m_failures = 0;

    RawFragment<3> m_fills[5];
    RawFragment<1> m_mark;

    void check(const char *name, bool pass, uint32_t stat, unsigned reads = 1);
    void checkMode(const char *name, bool pal, unsigned hres);
    void inFlight();
};

// psyqo's startup clears the BIOS exception handler queues and installs its
// own, which unhooks a resident debugger. The queue heads are saved before
// main, so before psyqo runs, and put back at exit.
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

ReinitTest app;
TestScene scene;

uint32_t gpustat() { return psyqo::Hardware::GPU::Ctrl; }
bool displayOff(uint32_t stat) { return (stat >> 23) & 1; }

// On an SCPH-5501, a GPUSTAT read issued right after a GP1(03h) write
// returned the old bit 23, so poll for the wanted value with a bound and
// report the count.
constexpr unsigned kMaxReads = 4096;
uint32_t settle(uint32_t mask, uint32_t want, unsigned &reads) {
    uint32_t stat = gpustat();
    for (reads = 1; ((stat & mask) != want) && (reads < kMaxReads); reads++) stat = gpustat();
    return stat;
}

psyqo::GPU::Configuration makeConfig(psyqo::GPU::Resolution res, psyqo::GPU::VideoMode mode) {
    psyqo::GPU::Configuration config;
    config.set(res)
        .set(mode)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE)
        .set(psyqo::GPU::MiscSetting::KEEP_VRAM);
    return config;
}

// GP0(C0h) for two pixels, after the GPU has drained its FIFO.
uint32_t readPixelPair(uint32_t x, uint32_t y) {
    for (unsigned i = 0; i < 2000000 && (gpustat() & 0x14000000) != 0x14000000; i++);
    psyqo::Hardware::GPU::Data = 0xc0000000;
    psyqo::Hardware::GPU::Data = (y << 16) | x;
    psyqo::Hardware::GPU::Data = (1u << 16) | 2u;
    for (unsigned i = 0; i < 100000 && !(gpustat() & 0x08000000); i++);
    return psyqo::Hardware::GPU::Data;
}

}  // namespace

void ReinitTest::prepare() { gpu().initialize(makeConfig(psyqo::GPU::Resolution::W320, psyqo::GPU::VideoMode::NTSC)); }

void ReinitTest::createScene() { pushScene(&scene); }

void TestScene::check(const char *name, bool pass, uint32_t stat, unsigned reads) {
    if (!pass) m_failures++;
    ramsyscall_printf("%-34s gpustat %08x  reads %-4d  %s\n", name, stat, (int)reads, pass ? "PASS" : "FAIL");
}

void TestScene::checkMode(const char *name, bool pal, unsigned hres) {
    app.gpu().waitReady();
    unsigned reads;
    uint32_t want = (pal ? (1u << 20) : 0u) | (hres << 17);
    uint32_t stat = settle(0x00960000, want, reads);
    check(name, (stat & 0x00960000) == want, stat, reads);
}

void TestScene::inFlight() {
    // Four 512x256 fills keep the GPU busy long enough for the transfer to
    // still be running when sendChain() returns.
    for (int i = 0; i < 4; i++) {
        m_fills[i].words[0] = 0x02000000 | (i * 0x101010);
        m_fills[i].words[1] = (uint32_t((i >> 1) * 256) << 16) | uint32_t((i & 1) * 512);
        m_fills[i].words[2] = (256u << 16) | 512u;
        app.gpu().chain(m_fills[i]);
    }
    // 0x00ff00 is 0x03e0 in 15 bits, a pair of pixels reads 0x03e003e0.
    m_fills[4].words[0] = 0x0200ff00;
    m_fills[4].words[1] = (uint32_t(kProbeY) << 16) | kProbeX;
    m_fills[4].words[2] = (1u << 16) | 16u;
    app.gpu().chain(m_fills[4]);
    m_mark.words[0] = 0xe1000000 | kMark;
    app.gpu().chain(m_mark);

    volatile bool done = false;
    app.gpu().sendChain([&done]() { done = true; }, psyqo::DMA::FROM_ISR);
    bool busy = (DMA_CTRL[DMA_GPU].CHCR & 0x01000000) != 0;
    app.gpu().reinitialize(makeConfig(psyqo::GPU::Resolution::W320, psyqo::GPU::VideoMode::NTSC));

    unsigned spins = 0;
    while (!done && spins < 2000000) {
        app.gpu().pumpCallbacks();
        spins++;
    }
    app.gpu().waitReady();
    unsigned reads;
    uint32_t stat = settle(0x00800000, 0, reads);
    uint32_t pixels = readPixelPair(kProbeX, kProbeY);
    ramsyscall_printf("in-flight: dma busy at reinitialize = %d, chain completed = %d, probe pixels %08x\n",
                      busy ? 1 : 0, done ? 1 : 0, pixels);
    if (!busy) {
        ramsyscall_printf("%-34s gpustat %08x  INCONCLUSIVE (transfer had finished)\n", "reinit during chain", stat);
        return;
    }
    check("reinit during chain", done && ((stat & 0x7ff) == 0) && (pixels == 0x03e003e0) && !displayOff(stat), stat,
          reads);
}

void TestScene::frame() {
    if (m_done) return;
    m_done = true;

    ramsyscall_printf("psyqo GPU::reinitialize\n");

    uint32_t stat;

    checkMode("initialize NTSC 320", false, 1);

    // The instrument: bit 23 has to be able to read 1.
    unsigned reads;
    psyqo::Hardware::GPU::Ctrl = 0x03000001;
    stat = settle(0x00800000, 0x00800000, reads);
    check("control: GP1(03h)=1 reads off", displayOff(stat), stat, reads);
    psyqo::Hardware::GPU::Ctrl = 0x03000000;
    stat = settle(0x00800000, 0, reads);
    check("control: GP1(03h)=0 reads on", !displayOff(stat), stat, reads);

    app.gpu().reinitialize(makeConfig(psyqo::GPU::Resolution::W256, psyqo::GPU::VideoMode::PAL));
    checkMode("reinitialize PAL 256", true, 0);

    app.gpu().reinitialize(makeConfig(psyqo::GPU::Resolution::W320, psyqo::GPU::VideoMode::NTSC));
    checkMode("reinitialize NTSC 320", false, 1);

    inFlight();

    if (m_failures == 0) {
        ramsyscall_printf("All reinitialize checks passed\n");
    } else {
        ramsyscall_printf("%d reinitialize check(s) FAILED\n", m_failures);
    }
    finish(m_failures == 0 ? 0 : 1);
}

int main() { return app.run(); }
