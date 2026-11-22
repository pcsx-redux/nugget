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

// Interrupts landing on a GTE command: the command has already executed, so
// the psyqo exception handler must return past it. A loop squares IR1 = 3 with
// SQR while VBlank interrupts land; any 81 is a command executed twice.
// Results are written to SIO1 by polling, since psyqo owns the kernel.

#include <stdint.h>

#include "psyqo/application.hh"
#include "psyqo/gpu.hh"
#include "psyqo/kernel.hh"
#include "psyqo/scene.hh"
#include "psyqo/xprintf.h"

extern "C" {
uint32_t irqcop2Loop(volatile uint32_t *frameCount, uint32_t frames, uint32_t *iterations);
}

namespace {

constexpr unsigned kFrames = 600;

class IrqCop2 final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class Cop2Scene final : public psyqo::Scene {
    void frame() override;
    bool m_done = false;
};

IrqCop2 app;
Cop2Scene scene;

void sioPut(char c) {
    while ((*reinterpret_cast<volatile uint32_t *>(0xbf801054) & 1) == 0);
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

}  // namespace

void IrqCop2::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void IrqCop2::createScene() { pushScene(&scene); }

void Cop2Scene::frame() {
    if (m_done) return;
    m_done = true;
    volatile uint32_t *frameCount = &psyqoFrameCounter;
    uint32_t iterations = 0;
    sioPrint("\nIRQCOP2 begin counter=%08x live=%d\n", reinterpret_cast<uintptr_t>(frameCount),
             app.gpu().getLiveFrameCount() - *frameCount);
    uint32_t before = *frameCount;
    uint32_t wrong = irqcop2Loop(frameCount, kFrames, &iterations);
    uint32_t after = *frameCount;
    sioPrint("IRQCOP2 frames=%d iterations=%d wrong=%d\n", after - before, iterations, wrong);
    sioPrint("IRQCOP2 end %s\n", (wrong == 0 && after - before == kFrames) ? "PASS" : "FAIL");
}

int main() {
    psyqo::Kernel::takeOverKernel();
    return app.run();
}
