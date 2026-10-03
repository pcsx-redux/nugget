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

// A plain switch-dispatch NMOS 6502 interpreter over a flat 64K RAM. No I/O
// mapping and no undocumented opcodes: this exists to measure what the CPU core
// alone costs. Cycle counts include page-crossing and taken-branch penalties.

namespace m6502 {

struct State {
    uint8_t* mem;
    uint32_t cycles;
    uint16_t pc;
    uint8_t a, x, y, s;
    uint8_t c, v, d, i;  // each 0 or 1
    uint8_t n;           // N is bit 7 of n
    uint8_t nz;          // Z is nz == 0
    // Recompiler interface. codeBits has one bit per guest byte that is covered by
    // translated code; a store to such a byte stops execution after the store with
    // dirty = address + 1, so the caller can invalidate before anything stale runs.
    const uint8_t* codeBits;
    void* const* table;
    int32_t budget;
    uint32_t reason;
    uint32_t dirty;
};

enum class Stop { Budget, Trap, Illegal, Smc };

// Instructions that end a translation block: control transfers, and the ones that
// change D (a block checks D once, on entry) or that the translator does not
// handle inline.
// clang-format off
static const uint8_t c_endsBlock[256] = {
    1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
};
// clang-format on

// clang-format off
static const uint8_t c_cycles[256] = {
    7,6,2,2,2,3,5,2,3,2,2,2,2,4,6,2, 2,5,2,2,2,4,6,2,2,4,2,2,2,4,7,2,
    6,6,2,2,3,3,5,2,4,2,2,2,4,4,6,2, 2,5,2,2,2,4,6,2,2,4,2,2,2,4,7,2,
    6,6,2,2,2,3,5,2,3,2,2,2,3,4,6,2, 2,5,2,2,2,4,6,2,2,4,2,2,2,4,7,2,
    6,6,2,2,2,3,5,2,4,2,2,2,5,4,6,2, 2,5,2,2,2,4,6,2,2,4,2,2,2,4,7,2,
    2,6,2,2,3,3,3,2,2,2,2,2,4,4,4,2, 2,6,2,2,4,4,4,2,2,5,2,2,2,5,2,2,
    2,6,2,2,3,3,3,2,2,2,2,2,4,4,4,2, 2,5,2,2,4,4,4,2,2,4,2,2,4,4,4,2,
    2,6,2,2,3,3,5,2,2,2,2,2,4,4,6,2, 2,5,2,2,2,4,6,2,2,4,2,2,2,4,7,2,
    2,6,2,2,3,3,5,2,2,2,2,2,4,4,6,2, 2,5,2,2,2,4,6,2,2,4,2,2,2,4,7,2,
};
// clang-format on

// N and Z are kept lazily as two copies of the last result, so an ALU op
// costs two register moves and no flag computation. They need separate copies
// because PLP and RTI can load N=1 Z=1, which no single byte expresses.

// The bus decides what an absolute-addressed access touches. Zero page, the
// stack, operand fetches and vectors always go straight to mem, so mem has to
// hold what the CPU sees there. A read-modify-write passes the old value too,
// because the 6502 writes it back once before the new one, and some I/O
// registers react to that first write. A write returning true ends the run after
// the current instruction, for I/O that moves the caller's next event. So does
// clearing I while irqPending() says an interrupt is waiting.
struct FlatBus {
    static inline uint32_t read(const uint8_t* mem, uint32_t a, uint32_t) { return mem[a]; }
    static inline bool write(uint8_t* mem, uint32_t a, uint8_t v, uint32_t) {
        mem[a] = v;
        return false;
    }
    static inline bool writeRmw(uint8_t* mem, uint32_t a, uint32_t, uint8_t v, uint32_t) {
        mem[a] = v;
        return false;
    }
    static inline bool irqPending() { return false; }
};

// Pushes PC and P with B clear, sets I, and loads PC from the vector.
static inline void interrupt(State& st, uint32_t vector) {
    uint8_t* mem = st.mem;
    mem[0x100 + st.s] = st.pc >> 8;
    st.s = (st.s - 1) & 0xff;
    mem[0x100 + st.s] = st.pc & 0xff;
    st.s = (st.s - 1) & 0xff;
    mem[0x100 + st.s] = (st.n & 0x80) | (st.v << 6) | 0x20 | (st.d << 3) | (st.i << 2) | ((st.nz == 0) << 1) | st.c;
    st.s = (st.s - 1) & 0xff;
    st.i = 1;
    st.pc = mem[vector] | (mem[vector + 1] << 8);
    st.cycles += 7;
}

// BlockMode stops after any c_endsBlock instruction. Smc enables the codeBits
// check on every store.
template <bool BlockMode = false, bool Smc = false, typename Bus = FlatBus>
__attribute__((noinline)) static Stop run(State& st, uint32_t budget) {
    uint8_t* const mem = st.mem;
    uint32_t cyc = st.cycles;
    uint32_t limit = cyc + budget;
    uint32_t pc = st.pc;
    uint32_t a = st.a, x = st.x, y = st.y, s = st.s;
    uint32_t c = st.c, v = st.v, d = st.d, i = st.i;
    uint32_t n = st.n, nz = st.nz;
    Stop why = Stop::Budget;
    bool smcHit = false;

#define RD(addr) (Bus::read(mem, (addr) & 0xffff, cyc))
#define SMCCHK(addr)                                                     \
    if (Smc) {                                                           \
        uint32_t sa_ = (addr) & 0xffff;                                  \
        if ((st.codeBits[sa_ >> 3] >> (sa_ & 7)) & 1) {                  \
            st.dirty = sa_ + 1;                                          \
            smcHit = true;                                               \
        }                                                                \
    }
#define WR(addr, val)                                    \
    {                                                    \
        uint32_t wa_ = (addr) & 0xffff;                  \
        if (Bus::write(mem, wa_, (uint8_t)(val), cyc)) limit = cyc; \
        SMCCHK(wa_);                                     \
    }
#define RMWWR(addr, old, val)                            \
    {                                                    \
        uint32_t wa_ = (addr) & 0xffff;                  \
        if (Bus::writeRmw(mem, wa_, old, (uint8_t)(val), cyc)) limit = cyc; \
        SMCCHK(wa_);                                     \
    }
#define IMM() (pc++, (pc - 1) & 0xffff)
#define ZP() (mem[pc++ & 0xffff])
#define ZPX() ((mem[pc++ & 0xffff] + x) & 0xff)
#define ZPY() ((mem[pc++ & 0xffff] + y) & 0xff)
#define ABS() (pc += 2, mem[(pc - 2) & 0xffff] | (mem[(pc - 1) & 0xffff] << 8))
#define ABSI(r, pen)                                \
    ({                                              \
        uint32_t base_ = ABS();                     \
        uint32_t ea_ = (base_ + (r)) & 0xffff;      \
        if (pen) cyc += ((base_ ^ ea_) >> 8) & 1;   \
        ea_;                                        \
    })
#define INDX()                                                              \
    ({                                                                      \
        uint32_t zp_ = (mem[pc++ & 0xffff] + x) & 0xff;                     \
        (uint32_t)(mem[zp_] | (mem[(zp_ + 1) & 0xff] << 8));                \
    })
#define INDY(pen)                                                           \
    ({                                                                      \
        uint32_t zp_ = mem[pc++ & 0xffff];                                  \
        uint32_t base_ = mem[zp_] | (mem[(zp_ + 1) & 0xff] << 8);           \
        uint32_t ea_ = (base_ + y) & 0xffff;                                \
        if (pen) cyc += ((base_ ^ ea_) >> 8) & 1;                           \
        ea_;                                                                \
    })
#define PUSH(val)                                                \
    {                                                            \
        mem[0x100 + s] = (uint8_t)(val);                         \
        SMCCHK(0x100 + s);                                       \
        s = (s - 1) & 0xff;                                      \
    }
#define PULL() (s = (s + 1) & 0xff, mem[0x100 + s])
#define BRANCH(cond)                                          \
    {                                                         \
        int32_t off_ = (int8_t)mem[pc++ & 0xffff];            \
        if (cond) {                                           \
            if (off_ == -2) {                                 \
                pc = (pc - 2) & 0xffff;                       \
                why = Stop::Trap;                             \
                goto out;                                     \
            }                                                 \
            uint32_t np_ = (pc + off_) & 0xffff;              \
            cyc += 1 + (((pc ^ np_) >> 8) & 1);               \
            pc = np_;                                         \
        }                                                     \
        break;                                                \
    }

#define ADC_OP(val)                                                         \
    {                                                                       \
        uint32_t m_ = (val);                                                \
        if (!d) {                                                           \
            uint32_t r_ = a + m_ + c;                                       \
            v = ((~(a ^ m_) & (a ^ r_)) >> 7) & 1;                          \
            c = r_ >> 8;                                                    \
            a = n = nz = r_ & 0xff;                                             \
        } else {                                                            \
            uint32_t lo_ = (a & 0x0f) + (m_ & 0x0f) + c;                    \
            uint32_t hi_ = (a & 0xf0) + (m_ & 0xf0);                        \
            if (lo_ > 9) {                                                  \
                lo_ += 6;                                                   \
            }                                                               \
            if (lo_ > 0x0f) hi_ += 0x10;                                    \
            uint32_t bin_ = (a + m_ + c) & 0xff;                            \
            v = ((~(a ^ m_) & (a ^ hi_)) >> 7) & 1;                         \
            if (hi_ > 0x90) hi_ += 0x60;                                    \
            c = hi_ > 0xff;                                                 \
            a = ((lo_ & 0x0f) | hi_) & 0xff;                                \
            n = hi_;                                                        \
            nz = bin_;                             \
        }                                                                   \
    }
#define SBC_OP(val)                                                         \
    {                                                                       \
        uint32_t m_ = (val);                                                \
        uint32_t r_ = a - m_ - (c ^ 1);                                     \
        v = (((a ^ m_) & (a ^ r_)) >> 7) & 1;                               \
        if (d) {                                                            \
            int32_t lo_ = (int32_t)(a & 0x0f) - (int32_t)(m_ & 0x0f) - (int32_t)(c ^ 1); \
            int32_t hi_ = (int32_t)(a & 0xf0) - (int32_t)(m_ & 0xf0);       \
            if (lo_ < 0) {                                                  \
                lo_ -= 6;                                                   \
                hi_ -= 0x10;                                                \
            }                                                               \
            if (hi_ < 0) hi_ -= 0x60;                                       \
            c = (r_ >> 8) ? 0 : 1;                                          \
            n = nz = r_ & 0xff;                                                 \
            a = ((lo_ & 0x0f) | (hi_ & 0xf0)) & 0xff;                       \
        } else {                                                            \
            c = (r_ >> 8) ? 0 : 1;                                          \
            a = n = nz = r_ & 0xff;                                             \
        }                                                                   \
    }
#define CMP_OP(reg, val)            \
    {                               \
        uint32_t r_ = (reg) - (val); \
        c = (r_ >> 8) ? 0 : 1;      \
        n = nz = r_ & 0xff;             \
    }
#define BIT_OP(val)                       \
    {                                     \
        uint32_t m_ = (val);              \
        v = (m_ >> 6) & 1;                \
        n = m_;                           \
        nz = a & m_;                      \
    }
#define ASL_M(ea)                    \
    {                                \
        uint32_t ea2_ = (ea);        \
        uint32_t m_ = RD(ea2_);      \
        c = m_ >> 7;                 \
        n = nz = (m_ << 1) & 0xff;       \
        RMWWR(ea2_, m_, nz);         \
    }
#define LSR_M(ea)                    \
    {                                \
        uint32_t ea2_ = (ea);        \
        uint32_t m_ = RD(ea2_);      \
        c = m_ & 1;                  \
        n = nz = m_ >> 1;                \
        RMWWR(ea2_, m_, nz);         \
    }
#define ROL_M(ea)                    \
    {                                \
        uint32_t ea2_ = (ea);        \
        uint32_t m_ = RD(ea2_);      \
        n = nz = ((m_ << 1) | c) & 0xff; \
        c = m_ >> 7;                 \
        RMWWR(ea2_, m_, nz);         \
    }
#define ROR_M(ea)                    \
    {                                \
        uint32_t ea2_ = (ea);        \
        uint32_t m_ = RD(ea2_);      \
        n = nz = (m_ >> 1) | (c << 7);   \
        c = m_ & 1;                  \
        RMWWR(ea2_, m_, nz);         \
    }
#define INC_M(ea)                         \
    {                                     \
        uint32_t ea2_ = (ea);             \
        uint32_t m_ = RD(ea2_);           \
        n = nz = (m_ + 1) & 0xff;         \
        RMWWR(ea2_, m_, nz);              \
    }
#define DEC_M(ea)                         \
    {                                     \
        uint32_t ea2_ = (ea);             \
        uint32_t m_ = RD(ea2_);           \
        n = nz = (m_ - 1) & 0xff;         \
        RMWWR(ea2_, m_, nz);              \
    }
#define PACKP(b) \
    ((n & 0x80) | (v << 6) | 0x20 | ((b) << 4) | (d << 3) | (i << 2) | ((nz == 0) << 1) | c)
#define UNPACKP(p)               \
    {                            \
        uint32_t p_ = (p);       \
        c = p_ & 1;              \
        i = (p_ >> 2) & 1;       \
        d = (p_ >> 3) & 1;       \
        v = (p_ >> 6) & 1;       \
        n = p_;                  \
        nz = (~p_ >> 1) & 1;     \
    }

    while (cyc < limit) {
        uint32_t op = mem[pc];
        pc = (pc + 1) & 0xffff;
        cyc += c_cycles[op];
        switch (op) {
            // loads
            case 0xa9: a = n = nz = mem[IMM()]; break;
            case 0xa5: a = n = nz = mem[ZP()]; break;
            case 0xb5: a = n = nz = mem[ZPX()]; break;
            case 0xad: a = n = nz = RD(ABS()); break;
            case 0xbd: a = n = nz = RD(ABSI(x, 1)); break;
            case 0xb9: a = n = nz = RD(ABSI(y, 1)); break;
            case 0xa1: a = n = nz = RD(INDX()); break;
            case 0xb1: a = n = nz = RD(INDY(1)); break;
            case 0xa2: x = n = nz = mem[IMM()]; break;
            case 0xa6: x = n = nz = mem[ZP()]; break;
            case 0xb6: x = n = nz = mem[ZPY()]; break;
            case 0xae: x = n = nz = RD(ABS()); break;
            case 0xbe: x = n = nz = RD(ABSI(y, 1)); break;
            case 0xa0: y = n = nz = mem[IMM()]; break;
            case 0xa4: y = n = nz = mem[ZP()]; break;
            case 0xb4: y = n = nz = mem[ZPX()]; break;
            case 0xac: y = n = nz = RD(ABS()); break;
            case 0xbc: y = n = nz = RD(ABSI(x, 1)); break;
            // stores
            case 0x85: WR(ZP(), a); break;
            case 0x95: WR(ZPX(), a); break;
            case 0x8d: WR(ABS(), a); break;
            case 0x9d: WR(ABSI(x, 0), a); break;
            case 0x99: WR(ABSI(y, 0), a); break;
            case 0x81: WR(INDX(), a); break;
            case 0x91: WR(INDY(0), a); break;
            case 0x86: WR(ZP(), x); break;
            case 0x96: WR(ZPY(), x); break;
            case 0x8e: WR(ABS(), x); break;
            case 0x84: WR(ZP(), y); break;
            case 0x94: WR(ZPX(), y); break;
            case 0x8c: WR(ABS(), y); break;
            // transfers
            case 0xaa: x = n = nz = a; break;
            case 0xa8: y = n = nz = a; break;
            case 0x8a: a = n = nz = x; break;
            case 0x98: a = n = nz = y; break;
            case 0xba: x = n = nz = s; break;
            case 0x9a: s = x; break;
            // stack
            case 0x48: PUSH(a); break;
            case 0x68: a = n = nz = PULL(); break;
            case 0x08: PUSH(PACKP(1)); break;
            case 0x28:
                UNPACKP(PULL());
                if (!i && Bus::irqPending()) limit = cyc;
                break;
            // logic
            case 0x29: a = n = nz = a & mem[IMM()]; break;
            case 0x25: a = n = nz = a & mem[ZP()]; break;
            case 0x35: a = n = nz = a & mem[ZPX()]; break;
            case 0x2d: a = n = nz = a & RD(ABS()); break;
            case 0x3d: a = n = nz = a & RD(ABSI(x, 1)); break;
            case 0x39: a = n = nz = a & RD(ABSI(y, 1)); break;
            case 0x21: a = n = nz = a & RD(INDX()); break;
            case 0x31: a = n = nz = a & RD(INDY(1)); break;
            case 0x09: a = n = nz = a | mem[IMM()]; break;
            case 0x05: a = n = nz = a | mem[ZP()]; break;
            case 0x15: a = n = nz = a | mem[ZPX()]; break;
            case 0x0d: a = n = nz = a | RD(ABS()); break;
            case 0x1d: a = n = nz = a | RD(ABSI(x, 1)); break;
            case 0x19: a = n = nz = a | RD(ABSI(y, 1)); break;
            case 0x01: a = n = nz = a | RD(INDX()); break;
            case 0x11: a = n = nz = a | RD(INDY(1)); break;
            case 0x49: a = n = nz = a ^ mem[IMM()]; break;
            case 0x45: a = n = nz = a ^ mem[ZP()]; break;
            case 0x55: a = n = nz = a ^ mem[ZPX()]; break;
            case 0x4d: a = n = nz = a ^ RD(ABS()); break;
            case 0x5d: a = n = nz = a ^ RD(ABSI(x, 1)); break;
            case 0x59: a = n = nz = a ^ RD(ABSI(y, 1)); break;
            case 0x41: a = n = nz = a ^ RD(INDX()); break;
            case 0x51: a = n = nz = a ^ RD(INDY(1)); break;
            case 0x24: BIT_OP(mem[ZP()]); break;
            case 0x2c: BIT_OP(RD(ABS())); break;
            // arithmetic
            case 0x69: ADC_OP(mem[IMM()]); break;
            case 0x65: ADC_OP(mem[ZP()]); break;
            case 0x75: ADC_OP(mem[ZPX()]); break;
            case 0x6d: ADC_OP(RD(ABS())); break;
            case 0x7d: ADC_OP(RD(ABSI(x, 1))); break;
            case 0x79: ADC_OP(RD(ABSI(y, 1))); break;
            case 0x61: ADC_OP(RD(INDX())); break;
            case 0x71: ADC_OP(RD(INDY(1))); break;
            case 0xe9: SBC_OP(mem[IMM()]); break;
            case 0xe5: SBC_OP(mem[ZP()]); break;
            case 0xf5: SBC_OP(mem[ZPX()]); break;
            case 0xed: SBC_OP(RD(ABS())); break;
            case 0xfd: SBC_OP(RD(ABSI(x, 1))); break;
            case 0xf9: SBC_OP(RD(ABSI(y, 1))); break;
            case 0xe1: SBC_OP(RD(INDX())); break;
            case 0xf1: SBC_OP(RD(INDY(1))); break;
            case 0xc9: CMP_OP(a, mem[IMM()]); break;
            case 0xc5: CMP_OP(a, mem[ZP()]); break;
            case 0xd5: CMP_OP(a, mem[ZPX()]); break;
            case 0xcd: CMP_OP(a, RD(ABS())); break;
            case 0xdd: CMP_OP(a, RD(ABSI(x, 1))); break;
            case 0xd9: CMP_OP(a, RD(ABSI(y, 1))); break;
            case 0xc1: CMP_OP(a, RD(INDX())); break;
            case 0xd1: CMP_OP(a, RD(INDY(1))); break;
            case 0xe0: CMP_OP(x, mem[IMM()]); break;
            case 0xe4: CMP_OP(x, mem[ZP()]); break;
            case 0xec: CMP_OP(x, RD(ABS())); break;
            case 0xc0: CMP_OP(y, mem[IMM()]); break;
            case 0xc4: CMP_OP(y, mem[ZP()]); break;
            case 0xcc: CMP_OP(y, RD(ABS())); break;
            // increments
            case 0xe6: INC_M(ZP()); break;
            case 0xf6: INC_M(ZPX()); break;
            case 0xee: INC_M(ABS()); break;
            case 0xfe: INC_M(ABSI(x, 0)); break;
            case 0xc6: DEC_M(ZP()); break;
            case 0xd6: DEC_M(ZPX()); break;
            case 0xce: DEC_M(ABS()); break;
            case 0xde: DEC_M(ABSI(x, 0)); break;
            case 0xe8: x = n = nz = (x + 1) & 0xff; break;
            case 0xca: x = n = nz = (x - 1) & 0xff; break;
            case 0xc8: y = n = nz = (y + 1) & 0xff; break;
            case 0x88: y = n = nz = (y - 1) & 0xff; break;
            // shifts
            case 0x0a: c = a >> 7; a = n = nz = (a << 1) & 0xff; break;
            case 0x4a: c = a & 1; a = n = nz = a >> 1; break;
            case 0x2a: { uint32_t t = ((a << 1) | c) & 0xff; c = a >> 7; a = n = nz = t; break; }
            case 0x6a: { uint32_t t = (a >> 1) | (c << 7); c = a & 1; a = n = nz = t; break; }
            case 0x06: ASL_M(ZP()); break;
            case 0x16: ASL_M(ZPX()); break;
            case 0x0e: ASL_M(ABS()); break;
            case 0x1e: ASL_M(ABSI(x, 0)); break;
            case 0x46: LSR_M(ZP()); break;
            case 0x56: LSR_M(ZPX()); break;
            case 0x4e: LSR_M(ABS()); break;
            case 0x5e: LSR_M(ABSI(x, 0)); break;
            case 0x26: ROL_M(ZP()); break;
            case 0x36: ROL_M(ZPX()); break;
            case 0x2e: ROL_M(ABS()); break;
            case 0x3e: ROL_M(ABSI(x, 0)); break;
            case 0x66: ROR_M(ZP()); break;
            case 0x76: ROR_M(ZPX()); break;
            case 0x6e: ROR_M(ABS()); break;
            case 0x7e: ROR_M(ABSI(x, 0)); break;
            // jumps
            case 0x4c: {
                uint32_t t = ABS();
                if (t == ((pc - 3) & 0xffff)) {
                    pc = t;
                    why = Stop::Trap;
                    goto out;
                }
                pc = t;
                break;
            }
            case 0x6c: {
                uint32_t p = ABS();
                pc = mem[p] | (mem[(p & 0xff00) | ((p + 1) & 0xff)] << 8);
                break;
            }
            case 0x20: {
                uint32_t t = ABS();
                uint32_t r = (pc - 1) & 0xffff;
                PUSH(r >> 8);
                PUSH(r);
                pc = t;
                break;
            }
            case 0x60: {
                uint32_t lo = PULL();
                uint32_t hi = PULL();
                pc = ((lo | (hi << 8)) + 1) & 0xffff;
                break;
            }
            case 0x00: {
                uint32_t r = (pc + 1) & 0xffff;
                PUSH(r >> 8);
                PUSH(r);
                PUSH(PACKP(1));
                i = 1;
                pc = mem[0xfffe] | (mem[0xffff] << 8);
                break;
            }
            case 0x40: {
                UNPACKP(PULL());
                if (!i && Bus::irqPending()) limit = cyc;
                uint32_t lo = PULL();
                uint32_t hi = PULL();
                pc = lo | (hi << 8);
                break;
            }
            // branches
            case 0x10: BRANCH(!(n & 0x80));
            case 0x30: BRANCH(n & 0x80);
            case 0x50: BRANCH(!v);
            case 0x70: BRANCH(v);
            case 0x90: BRANCH(!c);
            case 0xb0: BRANCH(c);
            case 0xd0: BRANCH(nz != 0);
            case 0xf0: BRANCH(nz == 0);
            // flags
            case 0x18: c = 0; break;
            case 0x38: c = 1; break;
            case 0x58:
                i = 0;
                if (Bus::irqPending()) limit = cyc;
                break;
            case 0x78: i = 1; break;
            case 0xb8: v = 0; break;
            case 0xd8: d = 0; break;
            case 0xf8: d = 1; break;
            case 0xea: break;
            default:
                pc = (pc - 1) & 0xffff;
                cyc -= c_cycles[op];
                why = Stop::Illegal;
                goto out;
        }
        if (Smc && smcHit) {
            why = Stop::Smc;
            break;
        }
        if (BlockMode && c_endsBlock[op]) break;
    }
out:
    st.cycles = cyc;
    st.pc = pc;
    st.a = a;
    st.x = x;
    st.y = y;
    st.s = s;
    st.c = c;
    st.v = v;
    st.d = d;
    st.i = i;
    st.n = n;
    st.nz = nz;
    return why;
#undef RD
#undef WR
#undef RMWWR
#undef SMCCHK
#undef IMM
#undef ZP
#undef ZPX
#undef ZPY
#undef ABS
#undef ABSI
#undef INDX
#undef INDY
#undef PUSH
#undef PULL
#undef BRANCH
#undef ADC_OP
#undef SBC_OP
#undef CMP_OP
#undef BIT_OP
#undef ASL_M
#undef LSR_M
#undef ROL_M
#undef ROR_M
#undef INC_M
#undef DEC_M
#undef PACKP
#undef UNPACKP
}

}  // namespace m6502
