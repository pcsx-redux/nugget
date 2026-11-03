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

// GPU::getNextClear() chained through GPU::chain() in 640x480 interlaced NTSC,
// as an application clears its frame. The fill has to cover the 480 rows the
// display shows. VRAM is filled black before the last two frames, so only
// their clears are counted; rows 0 and 1 are left out for the v0 GPU quirk
// described in tests/psyqo-480i-flip.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/hardware/gpu.hh"
#include "psyqo/kernel.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/scene.hh"

namespace {

constexpr unsigned kFrames = 6;
constexpr unsigned kRows = 480;

class ClearTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class TestScene final : public psyqo::Scene {
    void frame() override;
    unsigned m_frame = 0;
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
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

ClearTest app;
TestScene scene;
uint32_t s_col[kRows];

uint32_t gpustat() { return psyqo::Hardware::GPU::Ctrl; }
void waitIdle() { for (unsigned i = 0; i < 2000000 && (gpustat() & 0x14000000) != 0x14000000; i++); }
void fillBlack(int16_t y) {
    psyqo::Hardware::GPU::Data = 0x02000000;
    psyqo::Hardware::GPU::Data = uint32_t(y) << 16;
    psyqo::Hardware::GPU::Data = (256 << 16) | 640;
}

}  // namespace

void ClearTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W640)
        .set(psyqo::GPU::VideoMode::NTSC)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::INTERLACED);
    gpu().initialize(config);
}

void ClearTest::createScene() { pushScene(&scene); }

void TestScene::frame() {
    unsigned i = m_frame++;
    if (i < kFrames) {
        if (i == kFrames - 2) {
            waitIdle();
            psyqo::Hardware::GPU::Data = 0xe1000400;
            fillBlack(0);
            fillBlack(256);
            waitIdle();
            psyqo::Hardware::GPU::Data = 0xe1000000;
        }
        auto &clear = m_clear[gpu().getParity()];
        gpu().getNextClear(clear.primitive, psyqo::Color{{.r = 0xff, .g = 0xff, .b = 0xff}});
        gpu().chain(clear);
        return;
    }
    waitIdle();
    psyqo::Hardware::GPU::Data = 0x01000000;
    psyqo::Hardware::GPU::Data = 0xc0000000;
    psyqo::Hardware::GPU::Data = 0;
    psyqo::Hardware::GPU::Data = (kRows << 16) | 2;
    for (unsigned n = 0; n < 100000 && !(gpustat() & 0x08000000); n++);
    for (unsigned y = 0; y < kRows; y++) s_col[y] = psyqo::Hardware::GPU::Data;
    unsigned written = 0;
    int first = -1, last = -1;
    for (unsigned y = 2; y < kRows; y++) {
        if (s_col[y] & 0x7fff7fff) {
            written++;
            continue;
        }
        if (first < 0) first = y;
        last = y;
    }
    bool pass = written == kRows - 2;
    if (!pass) ramsyscall_printf("rows not cleared: %d, first %d, last %d\n", (int)(kRows - 2 - written), first, last);
    ramsyscall_printf("rows 2-479 cleared by the last two frames: %d/478  %s\n", (int)written, pass ? "PASS" : "FAIL");
    ramsyscall_printf(pass ? "All 480i clear checks passed\n" : "480i clear checks FAILED\n");
    finish(pass ? 0 : 1);
}

int main() { return app.run(); }
