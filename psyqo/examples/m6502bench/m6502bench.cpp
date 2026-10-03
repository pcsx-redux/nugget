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

// Measures a plain switch-dispatch NMOS 6502 interpreter on real hardware, by
// running the Klaus Dormann functional test (a CPU-bound, deterministic 96.2M-
// cycle program that ends in a self-jump on success) and reporting achieved
// guest MHz. The question is whether a bare 6502 core on PS1 can hold a C64's
// ~0.985 MHz with headroom, before any recompiler is built.

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/gpu.hh"
#include "psyqo/scene.hh"

#include "functest.h"
#include "m6502.hh"
#include "m6502jit.hh"

namespace {

// Counter 2 in sysclk/8 free-running: 33.8688 MHz / 8 = 4233600 Hz.
static constexpr uint32_t c_ticksPerSecond = 33868800u / 8u;

// A 64KiB guest RAM. The functional-test image is copied in at start; the
// interpreter runs over this, not over the read-only c_functest.
static uint8_t s_ram[65536];

static m6502::State s_cpu;

// Read counter 2, accumulate a wrap-safe 32-bit tick total. Each inter-read gap
// is kept well under the 16-bit wrap window (15.5 ms) by sizing the chunk.
static uint16_t s_lastTick;
static uint32_t s_accumTicks;

static inline void tickReset() {
    s_accumTicks = 0;
    s_lastTick = COUNTERS[2].value;
}
static inline void tickAccumulate() {
    const uint16_t now = COUNTERS[2].value;
    s_accumTicks += (uint16_t)(now - s_lastTick);
    s_lastTick = now;
}

class Bench final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class BenchScene final : public psyqo::Scene {
    void frame() override;
    bool m_done = false;
};

Bench g_app;
BenchScene g_scene;

static void printDec(const char* label, uint32_t v) { ramsyscall_printf("%s%u\n", label, v); }

// Root counter 1 is in hblank mode under psyqo. Its 16-bit wrap is ~4 s, far
// longer than any chunk, so it cross-checks counter 2 against a wrap the chunk
// sizing failed to prevent.
static uint16_t s_lastHblank;
static uint32_t s_accumHblanks;

static bool runMode(bool jit) {
    for (uint32_t i = 0; i < 65536; i++) s_ram[i] = c_functest[i];

    s_cpu = m6502::State{};
    s_cpu.mem = s_ram;
    s_cpu.pc = 0x0400;
    s_cpu.s = 0xff;
    s_cpu.i = 1;
    s_cpu.nz = 1;
    if (jit) m6502jit::init(s_cpu);

    const char* tag = jit ? "JIT" : "INTERP";
    ramsyscall_printf("M6502 %s: start, functest entry 0x0400\n", tag);

    COUNTERS[2].mode = 0x0200;  // system clock / 8, free running

    // Fixed chunk: 20000 guest cycles is 11.6 ms at the interpreter's 1.73 MHz,
    // under the 15.5 ms wrap, and shorter for anything faster. Compilation can
    // still stretch one chunk, which the hblank cross-check would show.
    const uint32_t chunk = 20000;
    tickReset();
    s_accumHblanks = 0;
    s_lastHblank = COUNTERS[1].value;
    m6502::Stop why = m6502::Stop::Budget;
    uint32_t guard = 0;
    while (why == m6502::Stop::Budget) {
        why = jit ? m6502jit::run(s_cpu, chunk) : m6502::run(s_cpu, chunk);
        tickAccumulate();
        const uint16_t h = COUNTERS[1].value;
        s_accumHblanks += (uint16_t)(h - s_lastHblank);
        s_lastHblank = h;
        if (++guard > 100000) break;
#ifdef JIT_TRACE
        if ((guard & 63) == 0 || guard < 8) {
            const auto& js = m6502jit::stats();
            ramsyscall_printf("TRACE %u: cyc %u pc %04x comp %u chain %u bud %u smc %u ib %u\n", guard, s_cpu.cycles,
                              s_cpu.pc, js.compiled, js.exitsChain, js.exitsBudget, js.exitsSmc, js.interpBlocks);
        }
#endif
    }

    const uint32_t guestCycles = s_cpu.cycles;
    const uint32_t ticks = s_accumTicks;

    // All 32-bit: the freestanding libc has no 64-bit divide. 1 tick = 1/4233.6 ms.
    // A guest cycle per millisecond is a kHz.
    const uint32_t c_ticksPerMs = (c_ticksPerSecond + 500u) / 1000u;  // 4234
    uint32_t elapsedMs = ticks / c_ticksPerMs;
    if (elapsedMs == 0) elapsedMs = 1;
    const uint32_t kHz = guestCycles / elapsedMs;
    const bool pass = why == m6502::Stop::Trap && s_cpu.pc == 0x3469 && guestCycles == 96241367;

    ramsyscall_printf("M6502 %s: stop=%d (0=budget 1=trap 2=illegal) final_pc=%04x\n", tag, (int)why, s_cpu.pc);
    ramsyscall_printf("M6502 %s: PASS=%d (trap at 0x3469 after exactly 96241367 cycles)\n", tag, pass ? 1 : 0);
    ramsyscall_printf("M6502 %s: guest_cycles %u\n", tag, guestCycles);
    ramsyscall_printf("M6502 %s: elapsed_ms %u (ticks %u, hblanks %u)\n", tag, elapsedMs, ticks, s_accumHblanks);
    ramsyscall_printf("M6502 %s: guest_speed %u.%03u MHz (counter 2)\n", tag, kHz / 1000, kHz % 1000);
    // Counter 2 undercounts if any single chunk overran its 15.5 ms wrap, which a
    // compile burst can do. Hblanks cannot wrap within a chunk. NTSC line rate,
    // measured on SCPH-1001 against counter 2 during the interpreter run: 15736 Hz.
    uint32_t hMs = (uint32_t)((s_accumHblanks * 1000u) / 15736u);
    if (hMs == 0) hMs = 1;
    const uint32_t hkHz = guestCycles / hMs;
    ramsyscall_printf("M6502 %s: guest_speed_hblank %u.%03u MHz (elapsed %u ms)%s\n", tag, hkHz / 1000, hkHz % 1000,
                      hMs, (hMs > elapsedMs + elapsedMs / 50) ? "  ** COUNTER 2 WRAPPED, use this one **" : "");
    if (jit) {
        const auto& st = m6502jit::stats();
        ramsyscall_printf("M6502 JIT: compiled %u failed %u words %u flushes %u inval %u killed %u\n", st.compiled,
                          st.compileFailed, st.codeWords, st.flushes, st.invalidations, st.blocksKilled);
        ramsyscall_printf("M6502 JIT: exits chain %u budget %u decimal %u smc %u interpBlocks %u interpCycles %u\n",
                          st.exitsChain, st.exitsBudget, st.exitsDecimal, st.exitsSmc, st.interpBlocks,
                          st.interpCycles);
    }
    return pass;
}

static bool s_allPass;

static void runBench() {
#ifndef SKIP_INTERP
    bool a = runMode(false);
#else
    bool a = true;
#endif
    bool b = runMode(true);
    s_allPass = a && b;
    ramsyscall_printf("M6502: end\n");
}

void Bench::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void Bench::createScene() { pushScene(&g_scene); }

void BenchScene::frame() {
    if (m_done) return;
    m_done = true;
    runBench();
    pcsx_exit(s_allPass ? 0 : 1);
}

}  // namespace

int main() { return g_app.run(); }
