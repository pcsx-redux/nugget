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

#include "../m6502bench/m6502.hh"

// A C64 (NTSC, 6567R8 VIC-II) on top of the m6502 core. CPU and VIC-II share
// one clock: the machine runs one raster line at a time, 65 cycles each, and a
// bad line hands only 25 of them to the CPU.
//
// Memory: g_view is what the CPU sees, so the core's direct accesses (zero
// page, stack, operands, vectors) read the right thing. $0000-$9FFF and
// $C000-$CFFF are plain RAM and live in g_view only. The three banked regions
// ($A000, $D000, $E000) keep their RAM in g_ram and copy ROM or RAM into g_view
// when $01 changes. $D000-$DFFF reads go through ioRead while I/O is mapped.

namespace c64 {

static constexpr uint32_t c_linesPerFrame = 263;
static constexpr uint32_t c_cyclesPerLine = 65;
static constexpr uint32_t c_badLineSteal = 40;

extern uint8_t g_view[65536];
extern uint8_t g_ram[65536];
extern uint8_t g_color[1024];
extern uint8_t g_vic[64];
extern m6502::State g_cpu;
extern bool g_ioMapped;
extern uint32_t g_raster;
extern uint32_t g_stolen;
extern uint32_t g_frame;
extern const uint8_t* g_chargen;

// Keyboard matrix: bit r of g_keys[c] set means the key at column c, row r is
// held. Column is what CIA1 port A selects, row is what port B reads.
extern uint8_t g_keys[8];

struct Roms {
    const uint8_t* basic;
    const uint8_t* kernal;
    const uint8_t* chargen;
};

void reset(const Roms& roms);

// What the renderer needs from each raster line, recorded at the end of the
// line. Drawing happens after the frame, so the emulation loop stays small.
struct LineRegs {
    uint8_t d011, d016, d018, d020, d021;
    uint8_t bank;  // VIC bank, 0-3
    int8_t row;    // text row latched on this line's bad line, or the last one; -1 before any
    uint8_t bad;
};
extern LineRegs g_lines[c_linesPerFrame];
// Screen codes and colours of each text row, latched at its bad line.
extern uint8_t g_rowCodes[25][40];
extern uint8_t g_rowColors[25][40];

void runFrame();

#ifdef C64_PROF
// Sum of sysclk ticks spent inside the CPU core, and the number of core calls.
uint16_t profTick();
extern uint32_t g_profRun, g_profCalls;
#endif

uint32_t ioRead(uint32_t a, uint32_t cyc);
bool slowWrite(uint32_t a, uint8_t v, uint32_t cyc);
bool irqPending();

struct Bus {
    static inline uint32_t read(const uint8_t* mem, uint32_t a, uint32_t cyc) {
        if (((a >> 12) == 0xd) && g_ioMapped) return ioRead(a, cyc);
        return mem[a];
    }
    static inline bool direct(uint32_t a) { return (a - 2) < 0x9ffe || (a >> 12) == 0xc; }
    static inline bool write(uint8_t* mem, uint32_t a, uint8_t v, uint32_t cyc) {
        if (direct(a)) {
            mem[a] = v;
            return false;
        }
        return slowWrite(a, v, cyc);
    }
    static inline bool writeRmw(uint8_t* mem, uint32_t a, uint32_t old, uint8_t v, uint32_t cyc) {
        if (direct(a)) {
            mem[a] = v;
            return false;
        }
        slowWrite(a, old, cyc);
        return slowWrite(a, v, cyc);
    }
    static inline bool irqPending() { return c64::irqPending(); }
};

// The VIC-II's view of memory: 16K bank selected by CIA2, character ROM at
// $1000-$1FFF of banks 0 and 2, RAM everywhere else (never I/O or the other ROMs).
uint32_t vicBase();
static inline uint8_t ramByte(uint32_t a) {
    return (a < 0xa000 || (a >> 12) == 0xc) ? g_view[a] : g_ram[a];
}
static inline uint8_t vicRead(uint32_t a14) {
    const uint32_t base = vicBase();
    if (!(base & 0x4000) && (a14 & 0x3000) == 0x1000) return g_chargen[a14 & 0x0fff];
    return ramByte(base | (a14 & 0x3fff));
}

struct Stats {
    uint32_t illegal;
    uint32_t irqs;
    uint32_t nmis;
    uint32_t badLines;
};
const Stats& stats();

}  // namespace c64
