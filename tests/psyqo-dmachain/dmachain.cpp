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

// Covers the three code paths an oversized packet can reach. The marker has to
// sit in the LAST word of the oversized node itself: the padding is GP0(00h),
// so a short transfer of a NOP payload is invisible, and a trailing node would
// set the marker whether or not the oversized node transferred in full.
//
//   arm 1  over                 sendChain's head path, no predecessor to flag it
//   arm 2  small -> over        ISR case 2, flag arrives through MADR
//   arm 3  small -> big -> over ISR case 2 plain, then case 1's oversized branch
//
// Each arm pre-sets a different texpage so the readback separates "the whole
// payload arrived" from "it did not" without assuming which GPUSTAT bits stick.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/hardware/gpu.hh"
#include "psyqo/scene.hh"

namespace {

constexpr uint32_t kSentinel = 0x2aa;
constexpr uint32_t kMark = 0x555;
constexpr uint32_t kNop = 0x00000000;

template <size_t N>
struct RawFragment : public psyqo::Fragments::ChainEntry {
    RawFragment() {
        static_assert(sizeof(*this) == sizeof(uintptr_t) + sizeof(uint32_t) * N,
                      "Spurious padding in raw fragment");
    }
    constexpr size_t getActualFragmentSize() const { return N; }
    void fillNops() {
        for (size_t i = 0; i < N; i++) words[i] = kNop;
    }
    void fillMarked() {
        for (size_t i = 0; i < N - 1; i++) words[i] = kNop;
        words[N - 1] = 0xe1000000 | kMark;
    }
    uint32_t words[N];
};

class DmaChainTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class TestScene final : public psyqo::Scene {
    void frame() override;

    bool m_done = false;
    unsigned m_failures = 0;

    RawFragment<4> m_small;    // <= 14 words, stays in the linked list
    RawFragment<64> m_big;     // 15..255, excised by the bit 23 marker
    RawFragment<512> m_over;   // > 255, oversized packet, header holds 512/16

    static uint32_t status() { return psyqo::Hardware::GPU::Ctrl & uint32_t(0x7ff); }

    void begin();
    void finish(const char *name);
};

DmaChainTest app;
TestScene scene;

}  // namespace

void DmaChainTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void DmaChainTest::createScene() { pushScene(&scene); }

void TestScene::begin() {
    m_small.fillNops();
    m_big.fillNops();
    m_over.fillMarked();
    app.gpu().waitReady();
    psyqo::GPU::sendRaw(0xe1000000 | kSentinel);
    app.gpu().waitReady();
}

void TestScene::finish(const char *name) {
    app.gpu().sendChain();
    app.gpu().waitReady();

    uint32_t after = status();
    unsigned header = m_over.head >> 24;

    bool transferred = after == kMark;
    bool encoded = header == 512 / 16;
    bool pass = transferred && encoded && (kSentinel != kMark);
    if (!pass) m_failures++;

    ramsyscall_printf("%-22s over.header=%-3d (want %-3d)  status %03x -> %03x  %s\n", name, (int)header,
                      (int)(512 / 16), (int)kSentinel, (int)after, pass ? "PASS" : "FAIL");
}

void TestScene::frame() {
    if (m_done) return;
    m_done = true;

    ramsyscall_printf("psyqo DMA chain, oversized packets\n");

    begin();
    app.gpu().chain(m_over);
    finish("over");

    begin();
    app.gpu().chain(m_small);
    app.gpu().chain(m_over);
    finish("small -> over");

    begin();
    app.gpu().chain(m_small);
    app.gpu().chain(m_big);
    app.gpu().chain(m_over);
    finish("small -> big -> over");

    if (m_failures == 0) {
        ramsyscall_printf("All 3 arms passed\n");
    } else {
        ramsyscall_printf("%d arm(s) FAILED\n", m_failures);
    }
    pcsx_exit(m_failures == 0 ? 0 : 1);
}

int main() { return app.run(); }
