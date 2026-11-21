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

// Checks the shadow call stack that BUILD=FastDebug fills in, and prints
// the call sites it captures three calls deep, to be read back with
//   mipsel-none-elf-addr2line -f -C -e shadow-stack.elf <addresses - 8>
// Built any other way, it says so and exits.

#include "psyqo/shadow-stack.hh"

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/coroutine.hh"
#include "psyqo/font.hh"
#include "psyqo/gpu.hh"
#include "psyqo/kernel.hh"
#include "psyqo/scene.hh"

namespace {

class ShadowStackTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::Font<> m_font;
};

class ShadowStackScene final : public psyqo::Scene {
    void frame() override;
    unsigned m_frames = 0;
    unsigned m_frameDepth = 0;
};

ShadowStackTest shadowStackTest;
ShadowStackScene shadowStackScene;

unsigned s_checks = 0;
unsigned s_failures = 0;

void check(bool pass, const char* what) {
    s_checks++;
    if (pass) return;
    s_failures++;
    ramsyscall_printf("shadow-stack: FAIL %s\n", what);
}

// What each level of the nested call sees as its own return address, which
// is the call site its entry hook pushed.
uintptr_t s_returnAddress[3];
uintptr_t s_captured[3];
unsigned s_capturedCount;
unsigned s_depthAtLevel3;

[[gnu::noinline]] void level3() {
    s_returnAddress[2] = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    s_depthAtLevel3 = psyqo::ShadowStack::depth();
    s_capturedCount = psyqo::ShadowStack::copy(s_captured, 3);
    __asm__ volatile("");
}

[[gnu::noinline]] void level2() {
    s_returnAddress[1] = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    level3();
    __asm__ volatile("");
}

[[gnu::noinline]] void level1() {
    s_returnAddress[0] = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    level2();
    __asm__ volatile("");
}

[[gnu::noinline]] bool nested(bool print) {
    unsigned base = psyqo::ShadowStack::depth();
    level1();
    bool pass = true;
    pass = pass && (s_depthAtLevel3 == base + 3);
    pass = pass && (s_capturedCount == 3);
    for (unsigned i = 0; i < 3; i++) pass = pass && (s_captured[i] == s_returnAddress[2 - i]);
    pass = pass && (psyqo::ShadowStack::depth() == base);
    if (print) {
        ramsyscall_printf("shadow-stack: depth %u at level3, %u back in the caller\n", s_depthAtLevel3,
                          psyqo::ShadowStack::depth());
        for (unsigned i = 0; i < s_capturedCount; i++) {
            ramsyscall_printf("shadow-stack: site %u %08x, return address %08x\n", i, s_captured[i],
                              s_returnAddress[2 - i]);
        }
    }
    return pass;
}

unsigned s_breaks = 0;

psyqo::Stackful<4096> s_coroutine;
unsigned s_coroutineDepthBefore;
unsigned s_coroutineDepthAfter;
uintptr_t s_coroutineSite;
uintptr_t s_coroutineReturnAddress;

[[gnu::noinline]] void coroutineInner() {
    s_coroutineReturnAddress = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    s_coroutineDepthBefore = psyqo::ShadowStack::depth();
    psyqo::ShadowStack::copy(&s_coroutineSite, 1);
    s_coroutine.yield();
    s_coroutineDepthAfter = psyqo::ShadowStack::depth();
    __asm__ volatile("");
}

[[gnu::noinline]] void coroutineBody() {
    coroutineInner();
    __asm__ volatile("");
}

void runChecks() {
    if constexpr (!psyqo::ShadowStack::enabled) {
        ramsyscall_printf("shadow-stack: not a FastDebug build, nothing to check\n");
        pcsx_exit(0);
        return;
    }
    check(nested(true), "nested calls");

    unsigned base = psyqo::ShadowStack::depth();
    s_coroutine.initialize([]() { coroutineBody(); });
    s_coroutine.resume();
    check(psyqo::ShadowStack::depth() == base, "main depth after the coroutine yielded");
    check(s_coroutineSite == s_coroutineReturnAddress, "coroutine call site");
    s_coroutine.resume();
    check(!s_coroutine.isAlive(), "coroutine returned");
    check(psyqo::ShadowStack::depth() == base, "main depth after the coroutine returned");
    check(s_coroutineDepthAfter == s_coroutineDepthBefore, "coroutine depth across the yield");
    ramsyscall_printf("shadow-stack: coroutine depth %u, main depth %u\n", s_coroutineDepthBefore, base);
    check(nested(false), "nested calls after the coroutine");

    // A handled break goes through the exception handler, which returns
    // to the code after it without unwinding anything.
    psyqo::Kernel::queuePsyqoBreakHandler([](uint32_t code) {
        if (code != 0x123) return false;
        s_breaks++;
        return true;
    });
    base = psyqo::ShadowStack::depth();
    for (unsigned i = 0; i < 4; i++) __asm__ volatile("break 14, 0x123");
    check(s_breaks == 4, "break handler ran");
    check(psyqo::ShadowStack::depth() == base, "depth after handled breaks");
    ramsyscall_printf("shadow-stack: %u breaks, depth %u before, %u after\n", s_breaks, base,
                      psyqo::ShadowStack::depth());
}

}  // namespace

void ShadowStackTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void ShadowStackTest::createScene() {
    m_font.uploadSystemFont(gpu());
    pushScene(&shadowStackScene);
    runChecks();
}

// Every frame draws, so DMA and VBlank interrupts land while the nested
// calls below push and pop.
void ShadowStackScene::frame() {
    unsigned depth = psyqo::ShadowStack::depth();
    if (m_frames == 0) m_frameDepth = depth;
    check(depth == m_frameDepth, "depth at each frame");
    for (unsigned i = 0; i < 200; i++) {
        if (!nested(false)) {
            check(false, "nested calls under interrupts");
            break;
        }
    }
    psyqo::Color bg{{.r = 0, .g = 64, .b = 91}};
    shadowStackTest.gpu().clear(bg);
    shadowStackTest.m_font.printf(shadowStackTest.gpu(), {{.x = 16, .y = 32}}, {{.r = 255, .g = 255, .b = 255}},
                                  "frame %u, depth %u", m_frames, depth);
    if (++m_frames == 60) {
        if (s_failures == 0) {
            ramsyscall_printf("shadow-stack: all %u checks passed\n", s_checks);
        } else {
            ramsyscall_printf("shadow-stack: %u of %u checks FAILED\n", s_failures, s_checks);
        }
        pcsx_exit(s_failures ? 1 : 0);
    }
}

int main() { return shadowStackTest.run(); }
