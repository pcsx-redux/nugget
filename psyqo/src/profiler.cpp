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
#include "psyqo/kernel.hh"
#include "psyqo/xprintf.h"

pcsx_ProfHeader* psyqo::Profiler::s_header = nullptr;
uint32_t psyqo::Profiler::s_cyclesPer16Lines = 0;
uint32_t psyqo::Profiler::s_nowCost = 0;
uint8_t psyqo::Profiler::s_depth = 0;
uint32_t psyqo::Profiler::s_generation = 0;
bool psyqo::Profiler::s_full = false;

namespace {

// Counts system clock cycles over 64 hblanks, starting on a line edge, with
// interrupts masked so no handler delays either edge, and returns a quarter
// of it. Each edge is only seen within one poll of the counters, so the
// window has to be long: a misread line period scales into the wrap choice
// of every long scope, and the 4 s limit needs it within a few parts in
// 10000. The window spans about two counter 2 wraps, counted as it polls.
[[gnu::no_instrument_function]] uint32_t measure16Lines() {
    volatile uint16_t* const counter1 = &COUNTERS[1].value;
    volatile uint16_t* const counter2 = &COUNTERS[2].value;
    psyqo::Kernel::fastEnterCriticalSection();
    uint16_t line = *counter1;
    while (*counter1 == line);
    uint16_t start = *counter2;
    uint16_t last = start;
    uint32_t wraps = 0;
    line = *counter1;
    while ((uint16_t)(*counter1 - line) < 64) {
        uint16_t now = *counter2;
        if (now < last) wraps++;
        last = now;
    }
    uint16_t end = *counter2;
    if (end < last) wraps++;
    psyqo::Kernel::fastLeaveCriticalSection();
    return ((wraps << 16) + end - start + 2) / 4;
}

// Buffers text for PCDRV, so a dump costs one host write per 512 bytes
// rather than one per field, and long names go out whole.
class PCDRVWriter {
  public:
    explicit PCDRVWriter(int fd) : m_fd(fd) {}
    void put(const char* str, unsigned len) {
        while (len) {
            unsigned chunk = sizeof(m_buffer) - m_used;
            if (chunk > len) chunk = len;
            __builtin_memcpy(m_buffer + m_used, str, chunk);
            m_used += chunk;
            str += chunk;
            len -= chunk;
            if (m_used == sizeof(m_buffer)) flush();
        }
    }
    void put(const char* str) {
        unsigned len = 0;
        while (str[len]) len++;
        put(str, len);
    }
    void flush() {
        if (m_used && (PCwrite(m_fd, m_buffer, m_used) != int(m_used))) m_ok = false;
        m_used = 0;
    }
    bool ok() const { return m_ok; }

  private:
    int m_fd;
    unsigned m_used = 0;
    bool m_ok = true;
    char m_buffer[512];
};

}  // namespace

void psyqo::Profiler::install(pcsx_ProfHeader* header, pcsx_ProfEntry* entries, unsigned capacity) {
    s_header = nullptr;
    s_generation++;
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
    s_full = false;
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
    s_generation++;
}

[[gnu::no_instrument_function]] void psyqo::Profiler::record(const Zone* zone, uint32_t start, uint32_t end,
                                                             uint16_t vsync, uint8_t depth, uint8_t stackDepth) {
    pcsx_ProfHeader* header = s_header;
    if (!header) return;
    uint32_t n = header->written;
    uint32_t mask = header->capacity - 1;
    pcsx_ProfEntry& entry = reinterpret_cast<pcsx_ProfEntry*>(header->entries)[n & mask];
    entry.zone = reinterpret_cast<uintptr_t>(zone);
    entry.start = start;
    entry.end = end;
    entry.vsync = vsync;
    entry.depth = depth;
    entry.stackDepth = stackDepth;
    // The entry has to be complete before a reader can see it.
    __asm__ volatile("" ::: "memory");
    header->written = n + 1;
    if ((n & mask) == mask) s_full = true;
}

[[gnu::no_instrument_function]] void psyqo::Profiler::Scope::begin() {
    m_header = s_header;
    m_generation = s_generation;
    m_vsync = *const_cast<const volatile uint32_t*>(&psyqoFrameCounter);
    m_depth = s_depth++;
    unsigned stackDepth = ShadowStack::depth();
    m_stackDepth = stackDepth > 255 ? 255 : stackDepth;
    m_start = now();
}

[[gnu::no_instrument_function]] void psyqo::Profiler::Scope::end() {
    uint32_t end = now();
    // A scope that straddles an install or uninstall touches nothing: its
    // times span a counter reset, and the depth now belongs to the new one.
    if (m_generation != s_generation) return;
    s_depth = m_depth;
    record(m_zone, m_start, end, m_vsync, m_depth, m_stackDepth);
}

bool psyqo::Profiler::dumpPCDRV(const char* path) {
    if (PCinit() < 0) return false;
    int fd = PCcreat(path, 0);
    if (fd < 0) return false;
    PCDRVWriter writer(fd);
    forEach([&writer](const Zone& zone, uint32_t start, uint32_t cycles, uint16_t vsync, uint8_t depth,
                      uint8_t stackDepth) {
        char numbers[64];
        int len = snprintf(numbers, sizeof(numbers), "%u %u %u %08x %u ", vsync, depth, stackDepth, start, cycles);
        writer.put(numbers, len);
        writer.put(zone.name);
        writer.put(" ", 1);
        writer.put(zone.file);
        len = snprintf(numbers, sizeof(numbers), ":%u\n", zone.line);
        writer.put(numbers, len);
    });
    writer.flush();
    bool closed = PCclose(fd) >= 0;
    return writer.ok() && closed;
}
