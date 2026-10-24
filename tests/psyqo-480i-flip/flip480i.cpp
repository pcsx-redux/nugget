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


// GPU::flip() in 640x480 interlaced NTSC, with GP0(E1h).10 clear as psyqo
// leaves it. The GPU then skips every row whose parity equals GPUSTAT.31 at
// draw time (see tests/gpu-dfe-480i), so each frame clears 640x480 right after
// flip() returns and consecutive frames have to land on opposite GPUSTAT.31
// values for two clears to cover all 480 rows. VRAM is cleared to black with
// the bit set before the last two frames, so only those two are counted.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/gpu.hh"
#include "psyqo/hardware/gpu.hh"
#include "psyqo/kernel.hh"
#include "psyqo/scene.hh"

namespace {

constexpr unsigned kFrames = 6;
constexpr unsigned kRows = 480;

class FlipTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class TestScene final : public psyqo::Scene {
    void frame() override;
    unsigned m_frame = 0;
    uint32_t m_bit31[kFrames];
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

FlipTest app;
TestScene scene;
uint32_t s_col[kRows];

uint32_t gpustat() { return psyqo::Hardware::GPU::Ctrl; }

void waitIdle() {
    for (unsigned i = 0; i < 2000000 && (gpustat() & 0x14000000) != 0x14000000; i++);
}

void fill(uint32_t color) {
    psyqo::Hardware::GPU::Data = 0x02000000 | color;
    psyqo::Hardware::GPU::Data = 0;
    psyqo::Hardware::GPU::Data = (kRows << 16) | 640;
}

}  // namespace

void FlipTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W640)
        .set(psyqo::GPU::VideoMode::NTSC)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::INTERLACED);
    gpu().initialize(config);
}

void FlipTest::createScene() { pushScene(&scene); }

void TestScene::frame() {
    unsigned i = m_frame++;
    if (i < kFrames) {
        m_bit31[i] = gpustat() >> 31;
        waitIdle();
        if (i == kFrames - 2) {
            psyqo::Hardware::GPU::Data = 0xe1000400;
            fill(0);
            waitIdle();
        }
        psyqo::Hardware::GPU::Data = 0xe1000000;
        fill(0xffffff);
        waitIdle();
        return;
    }

    psyqo::Hardware::GPU::Data = 0x01000000;
    psyqo::Hardware::GPU::Data = 0xc0000000;
    psyqo::Hardware::GPU::Data = 0;
    psyqo::Hardware::GPU::Data = (kRows << 16) | 2;
    for (unsigned n = 0; n < 100000 && !(gpustat() & 0x08000000); n++);
    for (unsigned y = 0; y < kRows; y++) s_col[y] = psyqo::Hardware::GPU::Data;
    // Rows 0 and 1 are left out: on a v0 GPU (SCPH-1000) a fill whose first row has the skipped
    // parity draws that row and skips the next one, so the 262-line field's clear leaves row 1.
    unsigned rows[2] = {0, 0};
    for (unsigned y = 2; y < kRows; y++) {
        bool written = (s_col[y] & 0x7fff7fff) != 0;
        rows[y & 1] += written;
        if (!written) ramsyscall_printf("row %d not written: %08x\n", (int)y, s_col[y]);
    }

    unsigned failures = 0;
    // Frame 0 has no history; from frame 1 on, GPUSTAT.31 must alternate.
    for (unsigned f = 0; f < kFrames; f++) {
        bool pass = (f < 2) || (m_bit31[f] != m_bit31[f - 1]);
        if (!pass) failures++;
        ramsyscall_printf("frame %d GPUSTAT.31 after flip = %d  %s\n", (int)f, (int)m_bit31[f], pass ? "PASS" : "FAIL");
    }
    bool covered = (rows[0] == kRows / 2 - 1) && (rows[1] == kRows / 2 - 1);
    if (!covered) failures++;
    ramsyscall_printf("rows 2-479 written by the last two frames: even %d/239, odd %d/239  %s\n", (int)rows[0],
                      (int)rows[1], covered ? "PASS" : "FAIL");
    ramsyscall_printf(failures == 0 ? "All 480i flip checks passed\n" : "480i flip checks FAILED\n");
    finish(failures == 0 ? 0 : 1);
}

int main() { return app.run(); }
