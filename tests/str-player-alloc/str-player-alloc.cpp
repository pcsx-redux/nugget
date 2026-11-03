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

// StrPlayer::play() with Options the heap cannot hold must return false and
// leave the heap as it found it. The CD-ROM device is never prepared: a play()
// that fails its allocation returns before touching the drive, and one that
// gets past it is already the failure being tested for.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo-paths/str-player.hh"
#include "psyqo/alloc.h"
#include "psyqo/application.hh"
#include "psyqo/cdrom-device.hh"
#include "psyqo/kernel.hh"
#include "psyqo/scene.hh"

namespace {

class AllocTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::CDRomDevice m_cdrom;
};

class TestScene final : public psyqo::Scene {
    void frame() override;
    bool m_done = false;
};

AllocTest app;
TestScene scene;
unsigned s_failures = 0;

void check(const char *name, bool pass, uint32_t a, uint32_t b) {
    if (!pass) s_failures++;
    ramsyscall_printf("%-40s %08x %08x  %s\n", name, a, b, pass ? "PASS" : "FAIL");
}

// The address a small allocation lands at. The allocator is first fit, so a
// play() that left a block behind moves it.
uintptr_t probe() {
    void *p = psyqo_malloc(16);
    psyqo_free(p);
    return reinterpret_cast<uintptr_t>(p);
}

void failsCleanly(const char *name, const psyqo::paths::StrPlayer::Options &options) {
    psyqo::paths::StrPlayer player(app.m_cdrom, app.gpu());
    const uintptr_t before = probe();
    ramsyscall_printf("%s: calling play()\n", name);
    const bool played = player.play(16, 1000, options);
    check(name, !played, played, 0);
    if (played) {
        // Past a failed allocation: nothing below would mean anything.
        ramsyscall_printf("str-player-alloc: play() accepted options the heap cannot hold\n");
        pcsx_exit(1);
        while (1);
    }
    const uintptr_t after = probe();
    check("  heap unchanged", before == after, before, after);
}

}  // namespace

void AllocTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}
void AllocTest::createScene() { pushScene(&scene); }

void TestScene::frame() {
    if (m_done) return;
    m_done = true;
    using Options = psyqo::paths::StrPlayer::Options;

    // The documented buffers of the default options: three slots of twelve
    // 2016-byte chunks, two 28672-halfword run-level buffers, two 24-bit
    // slices of 24 x 240 halfwords. heapNeeded() adds only bookkeeping.
    const uint32_t buffers = 3 * 12 * 2016 + 2 * 28672 * 2 + 2 * 24 * 240 * 2;
    const uint32_t need = psyqo::paths::StrPlayer::heapNeeded(Options{});
    check("heapNeeded(default) covers the buffers", need >= buffers && need <= buffers + 256, need, buffers);

    // 8 MB of run-level buffer, past the end of RAM.
    Options rl;
    rl.rlHalfwords = 0x400000;
    check("heapNeeded(rl) grows with rlHalfwords", psyqo::paths::StrPlayer::heapNeeded(rl) > 2 * 0x400000 * 2,
          psyqo::paths::StrPlayer::heapNeeded(rl), 0);
    failsCleanly("rlHalfwords too large: play() fails", rl);

    // The slot ring itself too large.
    Options slots;
    slots.frameSlots = 200;
    slots.maxSectorsPerFrame = 32;
    failsCleanly("slot ring too large: play() fails", slots);

    if (s_failures == 0) {
        ramsyscall_printf("str-player-alloc: all checks passed\n");
        pcsx_exit(0);
    } else {
        ramsyscall_printf("str-player-alloc: %u checks FAILED\n", s_failures);
        pcsx_exit(1);
    }
    while (1);
}

int main() { return app.run(); }
