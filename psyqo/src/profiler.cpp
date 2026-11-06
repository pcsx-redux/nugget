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

#include "psyqo/profiler.hh"

#include "common/hardware/counters.h"
#include "common/hardware/pcsxcmd.h"
#include "common/hardware/pcsxhw.h"
#include "common/kernel/pcdrv.h"
#include "psyqo/xprintf.h"

pcsx_ProfHeader* psyqo::Profiler::s_header = nullptr;
uint32_t psyqo::Profiler::s_cyclesPer16Lines = 0;
uint32_t psyqo::Profiler::s_nowCost = 0;
uint8_t psyqo::Profiler::s_depth = 0;

namespace {

// Counts system clock cycles over 16 hblanks, starting on a line edge. An
// interrupt landing on either edge skews one measurement, hence the median.
[[gnu::no_instrument_function]] uint32_t measure16Lines() {
    volatile uint16_t* const counter1 = &COUNTERS[1].value;
    volatile uint16_t* const counter2 = &COUNTERS[2].value;
    uint16_t line = *counter1;
    while (*counter1 == line);
    uint16_t start = *counter2;
    line = *counter1;
    while ((uint16_t)(*counter1 - line) < 16);
    return (uint16_t)(*counter2 - start);
}

}  // namespace

void psyqo::Profiler::install(pcsx_ProfHeader* header, pcsx_ProfEntry* entries, unsigned capacity) {
    s_header = nullptr;
    // System clock, free running to 0xffff, no interrupt. Writing the mode
    // also resets the counter.
    COUNTERS[2].mode = 0;

    uint32_t m[3];
    for (auto& v : m) v = measure16Lines();
    uint32_t lo = m[0] < m[1] ? m[0] : m[1];
    uint32_t hi = m[0] < m[1] ? m[1] : m[0];
    uint32_t cyclesPer16Lines = m[2] < lo ? lo : (m[2] > hi ? hi : m[2]);
    // NTSC is about 34450, PAL about 34690. Anything far off means counter 1
    // is not counting hblanks, and the times can only be approximate.
    if ((cyclesPer16Lines < 32000) || (cyclesPer16Lines > 37000)) cyclesPer16Lines = 34450;
    s_cyclesPer16Lines = cyclesPer16Lines;

    uint32_t a = now();
    uint32_t b = now();
    s_nowCost = elapsed(a, b);

    header->magic = PCSX_PROF_MAGIC;
    header->version = PCSX_PROF_VERSION;
    header->entrySize = sizeof(pcsx_ProfEntry);
    header->capacity = capacity;
    // GPUSTAT bit 20: PAL video mode.
    header->flags = (*reinterpret_cast<volatile uint32_t*>(0xbf801814) & (1 << 20)) ? PCSX_PROF_FLAG_PAL : 0;
    header->cyclesPer16Lines = cyclesPer16Lines;
    header->entries = reinterpret_cast<uintptr_t>(entries);
    header->written = 0;
    s_depth = 0;
    s_header = header;

    if (pcsx_present()) {
        pcsx_RegisterProfiler command;
        pcsx_commandInit(&command.header, PCSX_CMD_REGISTER_PROFILER, sizeof(command));
        command.profHeader = reinterpret_cast<uintptr_t>(header);
        pcsx_command(&command);
    }
}

void psyqo::Profiler::uninstall() {
    if (s_header && pcsx_present()) {
        pcsx_RegisterProfiler command;
        pcsx_commandInit(&command.header, PCSX_CMD_REGISTER_PROFILER, sizeof(command));
        command.profHeader = 0;
        pcsx_command(&command);
    }
    s_header = nullptr;
}

[[gnu::no_instrument_function]] void psyqo::Profiler::record(const Zone* zone, uint32_t start, uint32_t end,
                                                             uint16_t vsync, uint8_t depth, uint8_t stackDepth) {
    pcsx_ProfHeader* header = s_header;
    if (!header) return;
    uint32_t n = header->written;
    pcsx_ProfEntry& entry = reinterpret_cast<pcsx_ProfEntry*>(header->entries)[n & (header->capacity - 1)];
    entry.zone = reinterpret_cast<uintptr_t>(zone);
    entry.start = start;
    entry.end = end;
    entry.vsync = vsync;
    entry.depth = depth;
    entry.stackDepth = stackDepth;
    // The entry has to be complete before a reader can see it.
    __asm__ volatile("" ::: "memory");
    header->written = n + 1;
}

[[gnu::no_instrument_function]] void psyqo::Profiler::Scope::begin() {
    m_header = s_header;
    m_vsync = *const_cast<const volatile uint32_t*>(&psyqoFrameCounter);
    m_depth = s_depth++;
    unsigned stackDepth = ShadowStack::depth();
    m_stackDepth = stackDepth > 255 ? 255 : stackDepth;
    m_start = now();
}

[[gnu::no_instrument_function]] void psyqo::Profiler::Scope::end() {
    uint32_t end = now();
    s_depth = m_depth;
    // Only record into the buffer the scope started in.
    if (m_header == s_header) record(m_zone, m_start, end, m_vsync, m_depth, m_stackDepth);
}

bool psyqo::Profiler::dumpPCDRV(const char* path) {
    if (PCinit() < 0) return false;
    int fd = PCcreat(path, 0);
    if (fd < 0) return false;
    forEach([fd](const Zone& zone, uint32_t start, uint32_t cycles, uint16_t vsync, uint8_t depth, uint8_t stackDepth) {
        char line[160];
        int len = snprintf(line, sizeof(line), "%u %u %u %08x %u %s %s:%u\n", vsync, depth, stackDepth, start, cycles,
                           zone.name, zone.file, zone.line);
        if (len > int(sizeof(line)) - 1) len = sizeof(line) - 1;
        if (len > 0) PCwrite(fd, line, len);
    });
    PCclose(fd);
    return true;
}
