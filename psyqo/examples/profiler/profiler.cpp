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

// Scoped profiler example and self-check. Every frame runs a few nested,
// profiled workloads and shows their times; under PCSX-Redux, the Scope
// Profiler window draws them as a timeline. At start it checks that bench()
// times spin loops linearly from a few hundred cycles to a few million,
// which is what breaks first if the two-counter reconstruction is wrong,
// and that nested scopes record the right depths. It prints PASS or FAIL
// over the console.

#include "psyqo/profiler.hh"

#include <stdint.h>

#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/font.hh"
#include "psyqo/gpu.hh"
#include "psyqo/scene.hh"

namespace {

class ProfilerExample final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::Font<> m_font;
};

class ProfilerScene final : public psyqo::Scene {
    void frame() override;
};

ProfilerExample profilerExample;
ProfilerScene profilerScene;

psyqo::Profiler::Buffer<256> s_profile;

unsigned s_failures = 0;

void check(bool pass, const char* what) {
    if (pass) return;
    s_failures++;
    ramsyscall_printf("profiler: FAIL %s\n", what);
}

[[gnu::noinline]] void spin(unsigned iterations) {
    for (unsigned i = 0; i < iterations; i++) __asm__ volatile("");
}

void checkLinearity() {
    static constexpr psyqo::Profiler::Zone zone = {"spin", __FILE__, __LINE__};
    static constexpr unsigned c_sizes[] = {100, 1000, 10000, 100000, 1000000};
    // Cycles per 100 iterations.
    uint32_t perHundred[5];
    for (unsigned i = 0; i < 5; i++) {
        unsigned runs = c_sizes[i] >= 100000 ? 3 : 20;
        auto result = psyqo::Profiler::bench(zone, runs, [i]() { spin(c_sizes[i]); });
        check(result.count == runs, "bench ran every iteration");
        perHundred[i] = c_sizes[i] >= 100 ? result.min / (c_sizes[i] / 100) : 0;
        ramsyscall_printf("profiler: spin(%u) min %u max %u mean %u cycles\n", c_sizes[i], result.min, result.max,
                          result.mean());
    }
    // From 10000 iterations up, the loop body dominates the call, so the
    // cost per iteration has to agree to 1%. An error of one counter 2 wrap
    // is 65536 cycles, several percent of the larger runs.
    for (unsigned i = 3; i < 5; i++) {
        uint32_t a = perHundred[2], b = perHundred[i];
        uint32_t diff = a > b ? a - b : b - a;
        check(diff * 100 <= a, "cycles per iteration stay constant");
    }
}

[[gnu::noinline]] void inner() {
    PSYQO_PROFILE_SCOPE("inner");
    spin(2000);
}

[[gnu::noinline]] void outer() {
    PSYQO_PROFILE_SCOPE("outer");
    spin(1000);
    inner();
    inner();
}

void checkNesting() {
    static constexpr psyqo::Profiler::Zone marker = {"marker", __FILE__, __LINE__};
    psyqo::Profiler::bench(marker, 1, []() {});
    outer();
    // Oldest first: marker, inner, inner, outer.
    unsigned seen = 0;
    const psyqo::Profiler::Zone* zones[4];
    uint8_t depths[4];
    uint32_t cycles[4];
    psyqo::Profiler::forEach(
        [&](const psyqo::Profiler::Zone& zone, uint32_t, uint32_t duration, uint16_t, uint8_t depth, uint8_t) {
            if (&zone == &marker) seen = 0;
            if (seen < 4) {
                zones[seen] = &zone;
                depths[seen] = depth;
                cycles[seen] = duration;
            }
            seen++;
        });
    check(seen == 4, "four entries after the marker");
    if (seen != 4) return;
    check((zones[1] == zones[2]) && (zones[1]->name[0] == 'i'), "inner scopes recorded first");
    check(zones[3]->name[0] == 'o', "outer scope recorded last");
    check((depths[1] == 1) && (depths[2] == 1) && (depths[3] == 0), "nesting depths");
    check(cycles[3] > cycles[1] + cycles[2], "outer scope spans both inner ones");
    ramsyscall_printf("profiler: outer %u cycles, inner %u and %u\n", cycles[3], cycles[1], cycles[2]);
}

}  // namespace

void ProfilerExample::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void ProfilerExample::createScene() {
    m_font.uploadSystemFont(gpu());
    psyqo::Profiler::install(s_profile);
    ramsyscall_printf("profiler: %u cycles per 16 lines, %s\n", s_profile.header.cyclesPer16Lines,
                      (s_profile.header.flags & PCSX_PROF_FLAG_PAL) ? "PAL" : "NTSC");
    check((s_profile.header.cyclesPer16Lines > 34000) && (s_profile.header.cyclesPer16Lines < 35200),
          "cycles per line in range");
    checkLinearity();
    checkNesting();
    ramsyscall_printf("profiler: %s\n", s_failures ? "FAIL" : "PASS");
    pushScene(&profilerScene);
}

void ProfilerScene::frame() {
    PSYQO_PROFILE_SCOPE("frame");
    auto& font = profilerExample.m_font;
    {
        PSYQO_PROFILE_SCOPE("clear");
        gpu().clear();
    }
    uint32_t start = psyqo::Profiler::now();
    outer();
    uint32_t cycles = psyqo::Profiler::elapsed(start, psyqo::Profiler::now());
    {
        PSYQO_PROFILE_SCOPE("text");
        font.printf(gpu(), {{.x = 16, .y = 32}}, {{.r = 255, .g = 255, .b = 255}}, "outer(): %u cycles", cycles);
        font.printf(gpu(), {{.x = 16, .y = 48}}, {{.r = 255, .g = 255, .b = 255}}, "%u per 16 lines",
                    s_profile.header.cyclesPer16Lines);
        font.printf(gpu(), {{.x = 16, .y = 64}}, {{.r = 255, .g = 255, .b = 255}}, "self-check %s",
                    s_failures ? "FAILED" : "passed");
    }
}

int main() { return profilerExample.run(); }
