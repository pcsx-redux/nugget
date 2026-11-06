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

#pragma once

#include <stdint.h>

#include "common/hardware/pcsxprof.h"
#include "psyqo/shadow-stack.hh"

extern "C" uint32_t psyqoFrameCounter;

namespace psyqo {

/**
 * @brief Scoped profiler: timed scopes recorded into a ring buffer.
 *
 * @details A `PSYQO_PROFILE_SCOPE("name")` statement times the rest of its
 * enclosing block and records the zone, start, end, vsync count and nesting
 * depth into a ring buffer when the block exits. Nothing is linked unless the
 * program uses it, and nothing is recorded until a buffer is installed:
 *
 * @code
 * static psyqo::Profiler::Buffer<1024> s_profile;
 * // once, after the GPU is initialized:
 * psyqo::Profiler::install(s_profile);
 * // anywhere:
 * void Renderer::render() {
 *     PSYQO_PROFILE_SCOPE("render");
 *     ...
 * }
 * @endcode
 *
 * PCSX-Redux reads the buffer live and draws it as a timeline in its Scope
 * Profiler window. On a console, `forEach` or `dumpPCDRV` gets the entries
 * out.
 *
 * Time comes from root counter 2, which `install` claims and sets to
 * free-run on the system clock, combined with psyqo's hblank counter on root
 * counter 1. Every time is in system clock cycles, exact for scopes up to
 * about 4 seconds long. The layout of the buffer, and how the two counters
 * combine, are in `common/hardware/pcsxprof.h`.
 *
 * Scopes are meant for the main loop. The nesting depth is one global, so a
 * scope inside an interrupt handler, or one held across a coroutine
 * suspension while another scope runs, records the wrong depth. The times
 * stay right. A scope costs a few dozen cycles at each end, inside the
 * measured span of any scope around it.
 */
class Profiler {
  public:
    /**
     * @brief Static descriptor of one profiled scope. Its address is its
     * identity.
     */
    struct Zone {
        const char* name;
        const char* file;
        uint32_t line;
    };

    /**
     * @brief Storage for the ring buffer, with room for `Capacity` scopes.
     */
    template <unsigned Capacity>
    struct Buffer {
        static_assert(Capacity && !(Capacity & (Capacity - 1)), "Capacity must be a power of two");
        pcsx_ProfHeader header;
        pcsx_ProfEntry entries[Capacity];
    };

    /**
     * @brief Claims root counter 2 and starts recording into the buffer.
     *
     * @details Call it after the GPU is initialized, since the elapsed times
     * depend on the hblank counter it sets up. It spends about 50 lines
     * measuring how many cycles a line takes. Under PCSX-Redux, the buffer
     * is also registered so the emulator can read it. Installing another
     * buffer replaces the first one.
     */
    template <unsigned Capacity>
    static void install(Buffer<Capacity>& buffer) {
        install(&buffer.header, buffer.entries, Capacity);
    }

    /**
     * @brief Stops recording. Root counter 2 is left running.
     */
    static void uninstall();

    static bool installed() { return s_header != nullptr; }

    /**
     * @brief Reads a timestamp, as stored in the buffer.
     */
    [[gnu::always_inline, gnu::no_instrument_function]] static inline uint32_t now() {
        volatile uint16_t* const counter1 = reinterpret_cast<volatile uint16_t*>(0xbf801110);
        volatile uint16_t* const counter2 = reinterpret_cast<volatile uint16_t*>(0xbf801120);
        uint32_t lines, cycles;
        do {
            lines = *counter1;
            cycles = *counter2;
        } while (lines != *counter1);
        return (lines << 16) | cycles;
    }

    /**
     * @brief System clock cycles from timestamp `start` to timestamp `end`.
     */
    static uint32_t elapsed(uint32_t start, uint32_t end) { return pcsx_profElapsed(start, end, s_cyclesPer16Lines); }

    /**
     * @brief Times its own lifetime. Use `PSYQO_PROFILE_SCOPE` rather than
     * declaring one directly.
     */
    class Scope {
      public:
        [[gnu::always_inline]] explicit Scope(const Zone& zone) : m_zone(&zone) {
            if (s_header) begin();
        }
        [[gnu::always_inline]] ~Scope() {
            if (m_header) end();
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

      private:
        void begin();
        void end();
        const Zone* m_zone;
        pcsx_ProfHeader* m_header = nullptr;
        uint32_t m_start;
        uint16_t m_vsync;
        uint8_t m_depth;
        uint8_t m_stackDepth;
    };

    struct BenchResult {
        uint32_t count = 0;
        uint32_t min = 0;
        uint32_t max = 0;
        uint32_t total = 0;
        uint32_t mean() const { return count ? total / count : 0; }
    };

    /**
     * @brief Runs `function` `count` times, timing each run.
     *
     * @details Each run is recorded under `zone` like a scope, and the
     * result holds the run times in cycles. The cost of reading the counters
     * twice is subtracted. Needs an installed buffer: without one, nothing
     * runs and the result is empty. `total` wraps past 2^32 cycles, a bit
     * over two minutes.
     */
    template <typename F>
    static BenchResult bench(const Zone& zone, unsigned count, F&& function) {
        BenchResult result;
        if (!s_header) return result;
        result.min = 0xffffffff;
        for (unsigned i = 0; i < count; i++) {
            uint16_t vsync = *const_cast<const volatile uint32_t*>(&psyqoFrameCounter);
            uint32_t start = now();
            function();
            uint32_t end = now();
            record(&zone, start, end, vsync, s_depth, 0);
            uint32_t cycles = elapsed(start, end);
            cycles = cycles > s_nowCost ? cycles - s_nowCost : 0;
            if (cycles < result.min) result.min = cycles;
            if (cycles > result.max) result.max = cycles;
            result.total += cycles;
            result.count++;
        }
        return result;
    }

    /**
     * @brief Calls `function(zone, start, cycles, vsync, depth, stackDepth)`
     * for every entry still in the buffer, oldest first.
     *
     * @details `start` is the raw timestamp, `cycles` the scope's duration.
     * Do not record scopes from `function`.
     */
    template <typename F>
    static void forEach(F&& function) {
        pcsx_ProfHeader* header = s_header;
        if (!header) return;
        uint32_t written = header->written;
        uint32_t mask = header->capacity - 1;
        uint32_t first = written > header->capacity ? written - header->capacity : 0;
        auto entries = reinterpret_cast<const pcsx_ProfEntry*>(header->entries);
        for (uint32_t i = first; i < written; i++) {
            const pcsx_ProfEntry& entry = entries[i & mask];
            function(*reinterpret_cast<const Zone*>(entry.zone), entry.start, elapsed(entry.start, entry.end),
                     entry.vsync, entry.depth, entry.stackDepth);
        }
    }

    /**
     * @brief Writes every entry still in the buffer to a host file through
     * PCDRV, one line each, oldest first: vsync, depth, stack depth, start
     * timestamp in hex, cycles, zone name, file:line. Returns false if the
     * file could not be created.
     */
    static bool dumpPCDRV(const char* path);

  private:
    static void install(pcsx_ProfHeader* header, pcsx_ProfEntry* entries, unsigned capacity);
    static void record(const Zone* zone, uint32_t start, uint32_t end, uint16_t vsync, uint8_t depth,
                       uint8_t stackDepth);
    static pcsx_ProfHeader* s_header;
    static uint32_t s_cyclesPer16Lines;
    static uint32_t s_nowCost;
    static uint8_t s_depth;
    friend class Scope;
};

}  // namespace psyqo

#define PSYQO_PROFILE_CONCAT_(a, b) a##b
#define PSYQO_PROFILE_CONCAT(a, b) PSYQO_PROFILE_CONCAT_(a, b)

/**
 * @brief Profiles the rest of the enclosing block under `name`, a string
 * literal.
 */
#define PSYQO_PROFILE_SCOPE(name)                                                                               \
    static constexpr psyqo::Profiler::Zone PSYQO_PROFILE_CONCAT(psyqoProfileZone_, __LINE__) = {name, __FILE__, \
                                                                                                __LINE__};      \
    psyqo::Profiler::Scope PSYQO_PROFILE_CONCAT(psyqoProfileScope_, __LINE__) {                                 \
        PSYQO_PROFILE_CONCAT(psyqoProfileZone_, __LINE__)                                                       \
    }
