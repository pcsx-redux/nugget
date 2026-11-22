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

// Cost of the psyqo VBlank interrupt, measured from inside a running program.
// A loop running from KSEG1 reads root counter 2 (system clock) back to back;
// an interrupt shows up as one large delta. With "cold", 4 KB of code runs
// before each wait so the handler starts with none of its lines in the i-cache.
// Results are written to SIO1 by polling, since psyqo owns the kernel.

#include <stdint.h>

#include "psyqo/application.hh"
#include "psyqo/gpu.hh"
#include "psyqo/kernel.hh"
#include "psyqo/scene.hh"
#include "psyqo/xprintf.h"

extern "C" {
uint32_t irqcostGaps(uint16_t *out, uint32_t n, uint32_t cold, uint32_t thresh);
uint32_t irqcostNormal(uint32_t n);
}

namespace {

constexpr unsigned kWarm = 64;
constexpr unsigned kCold = 600;
constexpr unsigned kNormalIters = 500;

class IrqCost final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class CostScene final : public psyqo::Scene {
    void frame() override;
    bool m_done = false;
};

IrqCost app;
CostScene scene;
uint16_t s_warm[kWarm];
uint16_t s_cold[kCold];

volatile uint32_t &reg(uint32_t offset) { return *reinterpret_cast<volatile uint32_t *>(0xbf801000 + offset); }

void sioPut(char c) {
    while ((reg(0x54) & 1) == 0);
    *reinterpret_cast<volatile uint8_t *>(0xbf801050) = c;
}

void sioPrint(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vxprintf(
        [](const char *data, int size, void *) {
            for (int i = 0; i < size; i++) sioPut(data[i]);
        },
        nullptr, fmt, args);
    va_end(args);
}

template <typename F>
F *uncached(F *f) {
    return reinterpret_cast<F *>(reinterpret_cast<uintptr_t>(f) | 0xa0000000);
}

void dump(const char *tag, const uint16_t *v, unsigned n) {
    for (unsigned i = 0; i < n; i += 32) {
        sioPrint("%s", tag);
        for (unsigned j = i; j < n && j < i + 32; j++) sioPrint(" %d", v[j]);
        sioPrint("\n");
    }
}

}  // namespace

void IrqCost::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void IrqCost::createScene() { pushScene(&scene); }

void CostScene::frame() {
    if (m_done) return;
    m_done = true;
    sioPrint("\nIRQCOST begin vector");
    for (unsigned i = 0; i < 8; i++) sioPrint(" %08x", reinterpret_cast<volatile uint32_t *>(0x80000080)[i]);
    sioPrint("\n");

    // Root counter 2: system clock, free running over 16 bits.
    reg(0x124) = 0;

    uint32_t mask = reg(0x74);
    reg(0x74) = 0;
    uint32_t ticks[8];
    for (auto &t : ticks) t = uncached(irqcostNormal)(kNormalIters);
    uint32_t minTicks = ticks[0];
    for (auto t : ticks) minTicks = t < minTicks ? t : minTicks;
    sioPrint("NORMAL iters=%d ticks", kNormalIters);
    for (auto t : ticks) sioPrint(" %d", t);
    sioPrint("\n");
    uint32_t loop = (minTicks + kNormalIters / 2) / kNormalIters;
    // An interrupt can cost less than 64 cycles warm; 24 over the loop still sits above every
    // non-interrupt delta, which the frame count check below confirms.
    uint32_t thresh = loop + 24;
    sioPrint("LOOP %d.%02d thresh=%d\n", minTicks / kNormalIters, (minTicks % kNormalIters) * 100 / kNormalIters,
             thresh);

    // VBlank only.
    reg(0x74) = 1;
    uint32_t f0 = app.gpu().getLiveFrameCount();
    uncached(irqcostGaps)(s_warm, kWarm, 0, thresh);
    uint32_t f1 = app.gpu().getLiveFrameCount();
    uncached(irqcostGaps)(s_cold, kCold, 1, thresh);
    uint32_t f2 = app.gpu().getLiveFrameCount();
    reg(0x74) = mask;

    sioPrint("FRAMES warm=%d/%d cold=%d/%d\n", f1 - f0, kWarm, f2 - f1, kCold);
    dump("WARM", s_warm, kWarm);
    dump("COLD", s_cold, kCold);
    bool ok = (f1 - f0 == kWarm) && (f2 - f1 == kCold);
    sioPrint("IRQCOST end %s\n", ok ? "PASS" : "FAIL");
}

int main() {
    psyqo::Kernel::takeOverKernel();
    return app.run();
}
