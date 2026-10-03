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

#include "c64.hh"



namespace c64 {

uint8_t g_view[65536];
uint8_t g_ram[65536];
uint8_t g_color[1024];
uint8_t g_vic[64];
uint8_t g_keys[8];
m6502::State g_cpu;
bool g_ioMapped;
uint32_t g_raster;
uint32_t g_stolen;
uint32_t g_frame;
const uint8_t* g_chargen;

namespace {

Roms s_roms;
Stats s_stats;

// What each banked region currently shows: 0 RAM, 1 ROM, 2 I/O (D000 only).
uint8_t s_mapA, s_mapD, s_mapE;

uint8_t s_vicIrq;
bool s_denFrame;
uint32_t s_frameT0;   // machine cycle at which the current frame's line 0 starts
uint32_t s_nextLine;  // first line whose start has not been processed yet
uint32_t s_filled;    // g_lines[0, s_filled) hold their registers
int32_t s_row;        // text row latched at the last bad line
uint8_t s_sid[32];
uint32_t s_lfsr = 0x7ffff8;
bool s_nmiLine;

struct Cia {
    uint8_t pra, prb, ddra, ddrb;
    uint16_t ta, tb, latchA, latchB;
    uint8_t cra, crb, icr, mask;
    uint32_t last;
};
Cia s_cia1, s_cia2;

inline uint32_t now(uint32_t cyc) { return cyc + g_stolen; }

// The raster line a CPU cycle falls on.
inline uint32_t lineAt(uint32_t cyc) {
    const uint32_t l = (now(cyc) - s_frameT0) / c_cyclesPerLine;
    return l < c_linesPerFrame ? l : c_linesPerFrame - 1;
}

// Record the current VIC state into g_lines for every line before `upTo`. Called
// before any register write, so lines drawn with the old value keep it.
void fillLines(uint32_t upTo) {
    if (upTo > c_linesPerFrame) upTo = c_linesPerFrame;
    if (s_filled >= upTo) return;
    const uint8_t d011 = g_vic[0x11], d016 = g_vic[0x16], d018 = g_vic[0x18];
    const uint8_t d020 = g_vic[0x20], d021 = g_vic[0x21];
    const uint8_t bank = vicBase() >> 14;
    for (; s_filled < upTo; s_filled++) {
        LineRegs& lr = g_lines[s_filled];
        lr.d011 = d011;
        lr.d016 = d016;
        lr.d018 = d018;
        lr.d020 = d020;
        lr.d021 = d021;
        lr.bank = bank;
    }
}

void ciaUpdate(Cia& c, uint32_t t) {
    uint32_t el = t - c.last;
    c.last = t;
    if (el == 0) return;
    uint32_t ufA = 0;
    if ((c.cra & 0x21) == 0x01) {
        if (el <= c.ta) {
            c.ta -= el;
        } else {
            uint32_t rem = el - c.ta - 1;
            const uint32_t period = c.latchA + 1u;
            ufA = 1 + rem / period;
            c.ta = c.latchA - rem % period;
            if (c.cra & 0x08) {
                c.cra &= ~1;
                c.ta = c.latchA;
                ufA = 1;
            }
            c.icr |= 1;
        }
    }
    if (c.crb & 1) {
        const uint32_t mode = (c.crb >> 5) & 3;
        uint32_t count = mode == 0 ? el : mode == 2 ? ufA : 0;
        if (count) {
            if (count <= c.tb) {
                c.tb -= count;
            } else {
                uint32_t rem = count - c.tb - 1;
                const uint32_t period = c.latchB + 1u;
                c.tb = c.latchB - rem % period;
                if (c.crb & 0x08) {
                    c.crb &= ~1;
                    c.tb = c.latchB;
                }
                c.icr |= 2;
            }
        }
    }
}

uint8_t todReg(uint32_t r) {
    // A time-of-day clock good enough to read: derived from the frame count.
    const uint32_t tenths = g_frame / 6;
    switch (r) {
        case 8:
            return tenths % 10;
        case 9: {
            uint32_t s = (tenths / 10) % 60;
            return ((s / 10) << 4) | (s % 10);
        }
        case 10: {
            uint32_t m = (tenths / 600) % 60;
            return ((m / 10) << 4) | (m % 10);
        }
        default:
            return 1;
    }
}

uint8_t ciaRead(Cia& c, uint32_t r, uint32_t cyc, bool isCia1) {
    switch (r) {
        case 0: {
            uint8_t v = c.pra | ~c.ddra;
            if (!isCia1) {
                // IEC inputs: with nothing on the bus, CLK IN and DATA IN follow
                // our own (inverted) outputs.
                v = (v & 0x3f) | ((~c.pra & c.ddra & 0x10) ? 0x40 : 0) | ((~c.pra & c.ddra & 0x20) ? 0x80 : 0);
            }
            return v;
        }
        case 1: {
            uint8_t v = c.prb | ~c.ddrb;
            if (isCia1) {
                const uint8_t cols = c.pra | ~c.ddra;
                uint8_t rows = 0;
                for (uint32_t col = 0; col < 8; col++) {
                    if (!(cols & (1 << col))) rows |= g_keys[col];
                }
                v &= ~rows;
            }
            return v;
        }
        case 2:
            return c.ddra;
        case 3:
            return c.ddrb;
        case 4:
            ciaUpdate(c, now(cyc));
            return c.ta & 0xff;
        case 5:
            ciaUpdate(c, now(cyc));
            return c.ta >> 8;
        case 6:
            ciaUpdate(c, now(cyc));
            return c.tb & 0xff;
        case 7:
            ciaUpdate(c, now(cyc));
            return c.tb >> 8;
        case 8:
        case 9:
        case 10:
        case 11:
            return todReg(r);
        case 13: {
            ciaUpdate(c, now(cyc));
            uint8_t v = c.icr | ((c.icr & c.mask) ? 0x80 : 0);
            c.icr = 0;
            return v;
        }
        case 14:
            return c.cra;
        case 15:
            return c.crb;
        default:
            return 0;
    }
}

void ciaWrite(Cia& c, uint32_t r, uint8_t v, uint32_t cyc) {
    switch (r) {
        case 0:
            c.pra = v;
            break;
        case 1:
            c.prb = v;
            break;
        case 2:
            c.ddra = v;
            break;
        case 3:
            c.ddrb = v;
            break;
        case 4:
            c.latchA = (c.latchA & 0xff00) | v;
            break;
        case 5:
            ciaUpdate(c, now(cyc));
            c.latchA = (c.latchA & 0x00ff) | (v << 8);
            if (!(c.cra & 1)) c.ta = c.latchA;
            break;
        case 6:
            c.latchB = (c.latchB & 0xff00) | v;
            break;
        case 7:
            ciaUpdate(c, now(cyc));
            c.latchB = (c.latchB & 0x00ff) | (v << 8);
            if (!(c.crb & 1)) c.tb = c.latchB;
            break;
        case 13:
            if (v & 0x80) {
                c.mask |= v & 0x1f;
            } else {
                c.mask &= ~(v & 0x1f);
            }
            break;
        case 14:
            ciaUpdate(c, now(cyc));
            if (v & 0x10) c.ta = c.latchA;
            c.cra = v & ~0x10;
            break;
        case 15:
            ciaUpdate(c, now(cyc));
            if (v & 0x10) c.tb = c.latchB;
            c.crb = v & ~0x10;
            break;
        default:
            break;
    }
}

inline uint32_t rasterCompare() { return g_vic[0x12] | ((g_vic[0x11] & 0x80) << 1); }

void rebank() {
    const uint32_t p = (g_view[1] | ~g_view[0]) & 7;
    const bool lo = p & 1, hi = p & 2, ch = p & 4;
    const uint8_t mapA = (lo && hi) ? 1 : 0;
    const uint8_t mapE = hi ? 1 : 0;
    const uint8_t mapD = (lo || hi) ? (ch ? 2 : 1) : 0;
    if (mapA != s_mapA) {
        __builtin_memcpy(g_view + 0xa000, mapA ? s_roms.basic : g_ram + 0xa000, 0x2000);
        s_mapA = mapA;
    }
    if (mapE != s_mapE) {
        __builtin_memcpy(g_view + 0xe000, mapE ? s_roms.kernal : g_ram + 0xe000, 0x2000);
        s_mapE = mapE;
    }
    if (mapD != s_mapD) {
        if (mapD == 0) __builtin_memcpy(g_view + 0xd000, g_ram + 0xd000, 0x1000);
        if (mapD == 1) __builtin_memcpy(g_view + 0xd000, s_roms.chargen, 0x1000);
        s_mapD = mapD;
    }
    g_ioMapped = mapD == 2;
}

uint8_t vicRegRead(uint32_t r, uint32_t cyc) {
    switch (r) {
        case 0x11:
            return (g_vic[0x11] & 0x7f) | ((lineAt(cyc) & 0x100) >> 1);
        case 0x12:
            return lineAt(cyc) & 0xff;
        case 0x13:
        case 0x14:
            return 0;
        case 0x16:
            return g_vic[0x16] | 0xc0;
        case 0x18:
            return g_vic[0x18] | 0x01;
        case 0x19:
            return s_vicIrq | 0x70 | ((s_vicIrq & g_vic[0x1a]) ? 0x80 : 0);
        case 0x1a:
            return g_vic[0x1a] | 0xf0;
        case 0x1e:
        case 0x1f:
            return 0;
        default:
            if (r >= 0x20 && r <= 0x2e) return g_vic[r] | 0xf0;
            if (r > 0x2e) return 0xff;
            return g_vic[r];
    }
}

void vicRegWrite(uint32_t r, uint8_t v, uint32_t cyc) {
    const uint32_t line = lineAt(cyc);
    fillLines(line);
    switch (r) {
        case 0x11:
        case 0x12:
            g_vic[r] = v;
            if (rasterCompare() == line) s_vicIrq |= 1;
            break;
        case 0x19:
            s_vicIrq &= ~v & 0x0f;
            break;
        case 0x1a:
            g_vic[0x1a] = v & 0x0f;
            break;
        case 0x1e:
        case 0x1f:
            break;
        default:
            g_vic[r] = v;
            break;
    }
}

inline bool irqLine() { return (s_vicIrq & g_vic[0x1a]) || (s_cia1.icr & s_cia1.mask); }

}  // namespace

uint32_t vicBase() {
    const uint8_t pa = s_cia2.pra | ~s_cia2.ddra;
    return ((~pa) & 3) << 14;
}

uint32_t ioRead(uint32_t a, uint32_t cyc) {
    switch ((a >> 8) & 0x0f) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
            return vicRegRead(a & 0x3f, cyc);
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7: {
            const uint32_t r = a & 0x1f;
            if (r == 0x19 || r == 0x1a) return 0xff;
            if (r == 0x1b || r == 0x1c) {
                s_lfsr = (s_lfsr >> 1) | (((s_lfsr ^ (s_lfsr >> 5)) & 1) << 22);
                return s_lfsr & 0xff;
            }
            return 0;
        }
        case 0x8:
        case 0x9:
        case 0xa:
        case 0xb:
            return g_color[a & 0x3ff] | 0xf0;
        case 0xc:
            return ciaRead(s_cia1, a & 0x0f, cyc, true);
        case 0xd:
            return ciaRead(s_cia2, a & 0x0f, cyc, false);
        default:
            return 0xff;
    }
}

bool irqPending() { return irqLine(); }

bool slowWrite(uint32_t a, uint8_t v, uint32_t cyc) {
    if (a < 2) {
        g_view[a] = v;
        rebank();
        return false;
    }
    const uint32_t region = a >> 12;
    if (region == 0xd) {
        if (s_mapD == 2) {
            switch ((a >> 8) & 0x0f) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                    vicRegWrite(a & 0x3f, v, cyc);
                    return true;
                case 0x4:
                case 0x5:
                case 0x6:
                case 0x7:
                    s_sid[a & 0x1f] = v;
                    return false;
                case 0x8:
                case 0x9:
                case 0xa:
                case 0xb:
                    g_color[a & 0x3ff] = v & 0x0f;
                    return false;
                case 0xc:
                    ciaWrite(s_cia1, a & 0x0f, v, cyc);
                    return true;
                case 0xd:
                    fillLines(lineAt(cyc));
                    ciaWrite(s_cia2, a & 0x0f, v, cyc);
                    return true;
                default:
                    return false;
            }
        }
        g_ram[a] = v;
        if (s_mapD == 0) g_view[a] = v;
        return false;
    }
    g_ram[a] = v;
    if (region < 0xc) {
        if (s_mapA == 0) g_view[a] = v;
    } else {
        if (s_mapE == 0) g_view[a] = v;
    }
    return false;
}

void reset(const Roms& roms) {
    s_roms = roms;
    g_chargen = roms.chargen;
    __builtin_memset(g_view, 0, sizeof(g_view));
    __builtin_memset(g_ram, 0, sizeof(g_ram));
    // Power-on RAM is not zero on real machines; the common pattern is 64 bytes
    // of $00 then 64 of $FF. The ROMs do not care, but programs that peek at
    // uninitialised memory see something closer to hardware.
    for (uint32_t i = 0; i < 65536; i++) {
        uint8_t b = (i & 0x40) ? 0xff : 0x00;
        g_ram[i] = b;
        if (Bus::direct(i)) g_view[i] = b;
    }
    g_view[0] = 0;
    g_view[1] = 0;
    __builtin_memset(g_color, 0, sizeof(g_color));
    __builtin_memset(g_vic, 0, sizeof(g_vic));
    __builtin_memset(g_keys, 0, sizeof(g_keys));
    __builtin_memset(&s_cia1, 0, sizeof(s_cia1));
    __builtin_memset(&s_cia2, 0, sizeof(s_cia2));
    s_cia1.ta = s_cia1.tb = s_cia1.latchA = s_cia1.latchB = 0xffff;
    s_cia2.ta = s_cia2.tb = s_cia2.latchA = s_cia2.latchB = 0xffff;
    s_vicIrq = 0;
    s_mapA = s_mapD = s_mapE = 0xff;
    rebank();
    g_cpu = m6502::State{};
    g_cpu.mem = g_view;
    g_cpu.s = 0xfd;
    g_cpu.i = 1;
    g_cpu.nz = 1;
    g_cpu.pc = g_view[0xfffc] | (g_view[0xfffd] << 8);
    g_raster = 0;
    g_stolen = 0;
    g_frame = 0;
    s_frameT0 = 0;
    s_nmiLine = false;
    __builtin_memset(&s_stats, 0, sizeof(s_stats));
}

LineRegs g_lines[c_linesPerFrame];
#ifdef C64_PROF
uint32_t g_profRun, g_profCalls;
#endif
uint8_t g_rowCodes[25][40];
uint8_t g_rowColors[25][40];

namespace {

void latchRow(uint32_t row) {
    const uint32_t at = ((g_vic[0x18] & 0xf0) << 6) + row * 40;
    const uint32_t addr = vicBase() | at;
    // The screen is almost always in plain RAM outside the character ROM window.
    if ((addr + 39 < 0xa000 || (addr >> 12) == 0xc) && (addr & 0x7000) != 0x1000) {
        __builtin_memcpy(g_rowCodes[row], g_view + addr, 40);
    } else {
        for (uint32_t i = 0; i < 40; i++) g_rowCodes[row][i] = vicRead(at + i);
    }
    __builtin_memcpy(g_rowColors[row], g_color + row * 40, 40);
}

}  // namespace

namespace {

// The machine time of the next interrupt-enabled timer underflow, or `never`.
uint32_t ciaNextIrq(const Cia& c, uint32_t never) {
    uint32_t t = never;
    if ((c.cra & 0x21) == 0x01 && (c.mask & 1)) {
        const uint32_t u = c.last + c.ta + 1;
        if ((int32_t)(u - t) < 0) t = u;
    }
    if ((c.crb & 0x61) == 0x01 && (c.mask & 2)) {
        const uint32_t u = c.last + c.tb + 1;
        if ((int32_t)(u - t) < 0) t = u;
    }
    return t;
}

bool isBadLine(uint32_t line) {
    return s_denFrame && line >= 0x30 && line <= 0xf7 && (line & 7) == (g_vic[0x11] & 7u);
}

void lineStart(uint32_t line) {
    g_raster = line;
    if (line == 0x30) s_denFrame = g_vic[0x11] & 0x10;
    if (line == rasterCompare()) s_vicIrq |= 1;
    const bool bad = isBadLine(line);
    if (bad) {
        g_stolen += c_badLineSteal;
        s_stats.badLines++;
        if (s_row < 24) latchRow(++s_row);
    }
    g_lines[line].bad = bad;
    g_lines[line].row = s_row;
}

// The next line from `from` whose start the scheduler has to stop at: a bad
// line, the raster compare line, the DEN latch line, or the end of the frame.
uint32_t nextEventLine(uint32_t from) {
    uint32_t e = c_linesPerFrame;
    if (from <= 0x30) e = 0x30;
    const uint32_t cmp = rasterCompare();
    if (cmp >= from && cmp < e) e = cmp;
    if (s_denFrame) {
        uint32_t l = from < 0x30 ? 0x30 : from;
        const uint32_t ys = g_vic[0x11] & 7u;
        l += (ys - l) & 7;
        if (l <= 0xf7 && l < e) e = l;
    }
    return e;
}

}  // namespace

void runFrame() {
    s_nextLine = 0;
    s_filled = 0;
    s_row = -1;
    const uint32_t frameEnd = s_frameT0 + c_linesPerFrame * c_cyclesPerLine;
    for (;;) {
        uint32_t t = g_cpu.cycles + g_stolen;
        while (s_nextLine < c_linesPerFrame && (int32_t)(t - (s_frameT0 + s_nextLine * c_cyclesPerLine)) >= 0) {
            lineStart(s_nextLine++);
            t = g_cpu.cycles + g_stolen;
        }
        if ((int32_t)(t - frameEnd) >= 0) break;
        ciaUpdate(s_cia1, t);
        ciaUpdate(s_cia2, t);
        const bool nmi = (s_cia2.icr & s_cia2.mask) != 0;
        if (nmi && !s_nmiLine) {
            s_nmiLine = true;
            m6502::interrupt(g_cpu, 0xfffa);
            s_stats.nmis++;
            continue;
        }
        s_nmiLine = nmi;
        if (!g_cpu.i && irqLine()) {
            m6502::interrupt(g_cpu, 0xfffe);
            s_stats.irqs++;
            continue;
        }
        uint32_t e = s_frameT0 + nextEventLine(s_nextLine) * c_cyclesPerLine;
        e = ciaNextIrq(s_cia1, e);
        e = ciaNextIrq(s_cia2, e);
        int32_t budget = (int32_t)(e - t);
        if (budget <= 0) budget = 1;
#ifdef C64_PROF
        const uint16_t p0 = profTick();
#endif
        const m6502::Stop why = m6502::run<false, false, Bus>(g_cpu, budget);
#ifdef C64_PROF
        g_profRun += (uint16_t)(profTick() - p0);
        g_profCalls++;
#endif
        if (why == m6502::Stop::Trap || why == m6502::Stop::Illegal) {
            // JMP * or a branch to itself idles until the next event; an illegal
            // opcode jams the CPU the same way.
            if (why == m6502::Stop::Illegal) s_stats.illegal++;
            const uint32_t target = e - g_stolen;
            if ((int32_t)(target - g_cpu.cycles) > 0) g_cpu.cycles = target;
        }
    }
    fillLines(c_linesPerFrame);
    s_frameT0 = frameEnd;
    g_frame++;
}

const Stats& stats() { return s_stats; }

}  // namespace c64
