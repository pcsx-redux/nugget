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

#include "m6502jit.hh"

#include <stddef.h>
#include <stdint.h>

#include "common/syscalls/syscalls.h"

// The bus is picked at build time: a machine that wraps this file defines
// M6502JIT_BUS_HEADER and M6502JIT_BUS before including it.
#ifdef M6502JIT_BUS_HEADER
#include M6502JIT_BUS_HEADER
typedef M6502JIT_BUS JBus;
#else
typedef m6502::FlatBus JBus;
#endif

#ifndef M6502JIT_ARENA_WORDS
#define M6502JIT_ARENA_WORDS (256 * 1024)
#endif

using namespace m6502;

namespace {

// The interpreter runs through this so the dispatcher learns when an I/O write
// or a pending interrupt asked the caller to look at its events again.
bool s_stopReq;
struct SBus {
    static inline uint32_t read(const uint8_t* mem, uint32_t a, uint32_t cyc) { return JBus::read(mem, a, cyc); }
    static inline bool write(uint8_t* mem, uint32_t a, uint8_t v, uint32_t cyc) {
        if (!JBus::write(mem, a, v, cyc)) return false;
        s_stopReq = true;
        return true;
    }
    static inline bool writeRmw(uint8_t* mem, uint32_t a, uint32_t old, uint8_t v, uint32_t cyc) {
        if (!JBus::writeRmw(mem, a, old, v, cyc)) return false;
        s_stopReq = true;
        return true;
    }
    static inline bool irqPending() {
        if (!JBus::irqPending()) return false;
        s_stopReq = true;
        return true;
    }
    static inline bool smcVisible(uint32_t a) { return JBus::smcVisible(a); }
};
constexpr bool c_hasIo = JBus::hasIo;

// Pinned guest state while translated code runs. Everything in caller-saved
// registers is safe because translated code never calls out.
enum Reg {
    ZERO = 0,
    AT = 1,
    V0 = 2,
    V1 = 3,
    A0 = 4,
    A1 = 5,
    A2 = 6,
    A3 = 7,
    T0 = 8,
    T1 = 9,
    T2 = 10,
    T3 = 11,
    T4 = 12,
    T5 = 13,
    T6 = 14,
    T7 = 15,
    S0 = 16,
    S1 = 17,
    S2 = 18,
    S3 = 19,
    S4 = 20,
    S5 = 21,
    S6 = 22,
    S7 = 23,
    T8 = 24,
    T9 = 25,
    SP = 29,
    FP = 30,
    RA = 31,
};
constexpr int rA = S0, rX = S1, rY = S2, rS = S3;
constexpr int rZ = S4;    // Z is set iff rZ == 0
constexpr int rN = S5;    // N is bit 7 of rN
constexpr int rC = S6;    // 0 or 1
constexpr int rMEM = S7;  // guest RAM base + 0x8000, so every absolute address is a 16-bit offset
constexpr int rCYC = FP;  // remaining cycle budget, counts down
constexpr int rV = V1;    // 0 or 1
constexpr int rST = A2;   // State*
constexpr int rP = A0;    // 0x30 | D << 3 | I << 2: the P bits that are neither lazy nor pinned elsewhere
constexpr int rBITS = T8; // code bitmap; byte 8192 is "any code in page 1"
constexpr int rTAB = T9;  // block table

// EXIT_SLOW leaves before an instruction the translation cannot do (I/O, a
// write outside plain RAM) so the interpreter runs just that one. EXIT_IRQ
// leaves after something that cleared I, for the caller to check its line.
enum ExitReason : uint32_t {
    EXIT_CHAIN = 0,
    EXIT_BUDGET = 1,
    EXIT_DECIMAL = 2,
    EXIT_SMC = 3,
    EXIT_TRAP = 4,
    EXIT_SLOW = 5,
    EXIT_IRQ = 6,
};

constexpr uint32_t c_threshold = 2;
constexpr int c_maxInsns = 40;
constexpr uint32_t c_arenaWords = M6502JIT_ARENA_WORDS;
constexpr uint32_t c_maxBlocks = 4096;

uint32_t s_arena[c_arenaWords] __attribute__((aligned(16)));
uint32_t s_arenaBase;  // first word after the trampolines
uint32_t s_arenaUsed;
void* s_table[65536];
// Blocks translated for D = 1. A D = 0 block containing ADC/SBC redirects here
// on entry when D is set.
void* s_tableD[65536];
uint8_t s_hotD[65536];
uint8_t s_codeBits[8192 + 4];
// Visits before translation. c_noTranslate marks an entry point whose first
// instruction always needs the interpreter; it is single-stepped instead.
uint8_t s_hot[65536];
constexpr uint8_t c_noTranslate = 0xff;
bool s_failSlow;
// Bytes that have ever been written while covered by translated code. Blocks
// never start on or extend over them, so self-modifying code stays interpreted
// and does not churn compile/invalidate.
uint8_t s_smcHist[8192];

bool smcTouched(uint32_t pc, uint32_t len) {
    for (uint32_t a = pc; a < pc + len; a++) {
        if ((s_smcHist[(a & 0xffff) >> 3] >> (a & 7)) & 1) return true;
    }
    return false;
}

struct Block {
    uint32_t start, end;  // [start, end) guest bytes covered
    bool dec;
};
Block s_blocks[c_maxBlocks];
uint32_t s_numBlocks;

typedef void (*EnterFn)(void* code, State* st);
EnterFn s_enter;
uint32_t* s_exitCommon;

m6502jit::Stats s_stats;

// Opcode classification.
enum Kind : uint8_t {
    K_ILL = 0,
    K_LDA, K_LDX, K_LDY, K_STA, K_STX, K_STY,
    K_AND, K_ORA, K_EOR, K_ADC, K_SBC, K_CMP, K_CPX, K_CPY, K_BIT,
    K_INC, K_DEC, K_ASL, K_LSR, K_ROL, K_ROR,
    K_IMPL,
};
enum Mode : uint8_t { M_IMP, M_ACC, M_IMM, M_ZP, M_ZPX, M_ZPY, M_ABS, M_ABSX, M_ABSY, M_INDX, M_INDY, M_IND, M_REL };

uint8_t s_kind[256];
uint8_t s_mode[256];
uint8_t s_len[256];

struct OpDef {
    uint8_t op, kind, mode;
};
// clang-format off
const OpDef c_ops[] = {
    {0xa9,K_LDA,M_IMM},{0xa5,K_LDA,M_ZP},{0xb5,K_LDA,M_ZPX},{0xad,K_LDA,M_ABS},{0xbd,K_LDA,M_ABSX},{0xb9,K_LDA,M_ABSY},{0xa1,K_LDA,M_INDX},{0xb1,K_LDA,M_INDY},
    {0xa2,K_LDX,M_IMM},{0xa6,K_LDX,M_ZP},{0xb6,K_LDX,M_ZPY},{0xae,K_LDX,M_ABS},{0xbe,K_LDX,M_ABSY},
    {0xa0,K_LDY,M_IMM},{0xa4,K_LDY,M_ZP},{0xb4,K_LDY,M_ZPX},{0xac,K_LDY,M_ABS},{0xbc,K_LDY,M_ABSX},
    {0x85,K_STA,M_ZP},{0x95,K_STA,M_ZPX},{0x8d,K_STA,M_ABS},{0x9d,K_STA,M_ABSX},{0x99,K_STA,M_ABSY},{0x81,K_STA,M_INDX},{0x91,K_STA,M_INDY},
    {0x86,K_STX,M_ZP},{0x96,K_STX,M_ZPY},{0x8e,K_STX,M_ABS},
    {0x84,K_STY,M_ZP},{0x94,K_STY,M_ZPX},{0x8c,K_STY,M_ABS},
    {0x29,K_AND,M_IMM},{0x25,K_AND,M_ZP},{0x35,K_AND,M_ZPX},{0x2d,K_AND,M_ABS},{0x3d,K_AND,M_ABSX},{0x39,K_AND,M_ABSY},{0x21,K_AND,M_INDX},{0x31,K_AND,M_INDY},
    {0x09,K_ORA,M_IMM},{0x05,K_ORA,M_ZP},{0x15,K_ORA,M_ZPX},{0x0d,K_ORA,M_ABS},{0x1d,K_ORA,M_ABSX},{0x19,K_ORA,M_ABSY},{0x01,K_ORA,M_INDX},{0x11,K_ORA,M_INDY},
    {0x49,K_EOR,M_IMM},{0x45,K_EOR,M_ZP},{0x55,K_EOR,M_ZPX},{0x4d,K_EOR,M_ABS},{0x5d,K_EOR,M_ABSX},{0x59,K_EOR,M_ABSY},{0x41,K_EOR,M_INDX},{0x51,K_EOR,M_INDY},
    {0x69,K_ADC,M_IMM},{0x65,K_ADC,M_ZP},{0x75,K_ADC,M_ZPX},{0x6d,K_ADC,M_ABS},{0x7d,K_ADC,M_ABSX},{0x79,K_ADC,M_ABSY},{0x61,K_ADC,M_INDX},{0x71,K_ADC,M_INDY},
    {0xe9,K_SBC,M_IMM},{0xe5,K_SBC,M_ZP},{0xf5,K_SBC,M_ZPX},{0xed,K_SBC,M_ABS},{0xfd,K_SBC,M_ABSX},{0xf9,K_SBC,M_ABSY},{0xe1,K_SBC,M_INDX},{0xf1,K_SBC,M_INDY},
    {0xc9,K_CMP,M_IMM},{0xc5,K_CMP,M_ZP},{0xd5,K_CMP,M_ZPX},{0xcd,K_CMP,M_ABS},{0xdd,K_CMP,M_ABSX},{0xd9,K_CMP,M_ABSY},{0xc1,K_CMP,M_INDX},{0xd1,K_CMP,M_INDY},
    {0xe0,K_CPX,M_IMM},{0xe4,K_CPX,M_ZP},{0xec,K_CPX,M_ABS},
    {0xc0,K_CPY,M_IMM},{0xc4,K_CPY,M_ZP},{0xcc,K_CPY,M_ABS},
    {0x24,K_BIT,M_ZP},{0x2c,K_BIT,M_ABS},
    {0xe6,K_INC,M_ZP},{0xf6,K_INC,M_ZPX},{0xee,K_INC,M_ABS},{0xfe,K_INC,M_ABSX},
    {0xc6,K_DEC,M_ZP},{0xd6,K_DEC,M_ZPX},{0xce,K_DEC,M_ABS},{0xde,K_DEC,M_ABSX},
    {0x0a,K_ASL,M_ACC},{0x06,K_ASL,M_ZP},{0x16,K_ASL,M_ZPX},{0x0e,K_ASL,M_ABS},{0x1e,K_ASL,M_ABSX},
    {0x4a,K_LSR,M_ACC},{0x46,K_LSR,M_ZP},{0x56,K_LSR,M_ZPX},{0x4e,K_LSR,M_ABS},{0x5e,K_LSR,M_ABSX},
    {0x2a,K_ROL,M_ACC},{0x26,K_ROL,M_ZP},{0x36,K_ROL,M_ZPX},{0x2e,K_ROL,M_ABS},{0x3e,K_ROL,M_ABSX},
    {0x6a,K_ROR,M_ACC},{0x66,K_ROR,M_ZP},{0x76,K_ROR,M_ZPX},{0x6e,K_ROR,M_ABS},{0x7e,K_ROR,M_ABSX},
    {0xaa,K_IMPL,M_IMP},{0xa8,K_IMPL,M_IMP},{0x8a,K_IMPL,M_IMP},{0x98,K_IMPL,M_IMP},{0xba,K_IMPL,M_IMP},{0x9a,K_IMPL,M_IMP},
    {0x48,K_IMPL,M_IMP},{0x68,K_IMPL,M_IMP},{0x08,K_IMPL,M_IMP},{0x28,K_IMPL,M_IMP},
    {0xe8,K_IMPL,M_IMP},{0xca,K_IMPL,M_IMP},{0xc8,K_IMPL,M_IMP},{0x88,K_IMPL,M_IMP},
    {0x4c,K_IMPL,M_ABS},{0x6c,K_IMPL,M_IND},{0x20,K_IMPL,M_ABS},{0x60,K_IMPL,M_IMP},{0x00,K_IMPL,M_IMP},{0x40,K_IMPL,M_IMP},
    {0x10,K_IMPL,M_REL},{0x30,K_IMPL,M_REL},{0x50,K_IMPL,M_REL},{0x70,K_IMPL,M_REL},
    {0x90,K_IMPL,M_REL},{0xb0,K_IMPL,M_REL},{0xd0,K_IMPL,M_REL},{0xf0,K_IMPL,M_REL},
    {0x18,K_IMPL,M_IMP},{0x38,K_IMPL,M_IMP},{0x58,K_IMPL,M_IMP},{0x78,K_IMPL,M_IMP},
    {0xb8,K_IMPL,M_IMP},{0xd8,K_IMPL,M_IMP},{0xf8,K_IMPL,M_IMP},{0xea,K_IMPL,M_IMP},
};
// clang-format on

uint8_t modeLen(uint8_t m) {
    switch (m) {
        case M_IMP:
        case M_ACC:
            return 1;
        case M_ABS:
        case M_ABSX:
        case M_ABSY:
        case M_IND:
            return 3;
        default:
            return 2;
    }
}

bool isBranch(uint8_t op) { return (op & 0x1f) == 0x10; }

// Control transfers end a translation. PLP/SED/CLD do not: they get a D guard
// instead, when an ADC/SBC follows them in the block.
bool endsTranslation(uint8_t op) {
    if (c_hasIo && op == 0x58) return true;
    switch (op) {
        case 0x00:
        case 0x20:
        case 0x40:
        case 0x4c:
        case 0x60:
        case 0x6c:
            return true;
    }
    return false;
}
bool changesD(uint8_t op) { return op == 0x28 || op == 0xd8 || op == 0xf8; }

// Flag liveness masks, C = 1, V = 2.
uint8_t flagWrites(uint8_t op) {
    switch (s_kind[op]) {
        case K_ADC:
        case K_SBC:
            return 3;
        case K_CMP:
        case K_CPX:
        case K_CPY:
        case K_ASL:
        case K_LSR:
        case K_ROL:
        case K_ROR:
            return 1;
        case K_BIT:
            return 2;
        default:
            break;
    }
    switch (op) {
        case 0x18:
        case 0x38:
            return 1;
        case 0xb8:
            return 2;
        case 0x28:
        case 0x40:
            return 3;
    }
    return 0;
}
uint8_t flagReads(uint8_t op) {
    switch (s_kind[op]) {
        case K_ADC:
        case K_SBC:
        case K_ROL:
        case K_ROR:
            return 1;
        default:
            break;
    }
    switch (op) {
        case 0x90:
        case 0xb0:
            return 1;
        case 0x50:
        case 0x70:
            return 2;
        case 0x08:
        case 0x00:
            return 3;
    }
    return 0;
}
bool mayExit(uint8_t op) {
    switch (s_kind[op]) {
        case K_STA:
        case K_STX:
        case K_STY:
        case K_INC:
        case K_DEC:
            return true;
        case K_ASL:
        case K_LSR:
        case K_ROL:
        case K_ROR:
            return s_mode[op] != M_ACC;
        default:
            break;
    }
    if (isBranch(op)) return true;
    switch (op) {
        case 0x48:
        case 0x08:
        case 0x20:
        case 0x00:
        case 0x28:
        case 0xd8:
        case 0xf8:
            return true;
        case 0x58:
            return c_hasIo;
    }
    return false;
}

bool isReadKind(uint8_t k) { return k >= K_LDA && k <= K_BIT && k != K_STA && k != K_STX && k != K_STY; }
bool isWriteKind(uint8_t k) { return k == K_STA || k == K_STX || k == K_STY; }
bool isRmwKind(uint8_t k) { return k >= K_INC && k <= K_ROR; }
bool isIndexed(uint8_t m) { return m == M_ZPX || m == M_ZPY || m == M_ABSX || m == M_ABSY || m == M_INDX || m == M_INDY; }

// Whether any address an indexed absolute access can reach satisfies pred.
template <typename F>
bool anyInRange(uint32_t base, F pred) {
    for (uint32_t i = 0; i < 256; i++) {
        if (pred((base + i) & 0xffff)) return true;
    }
    return false;
}

// An instruction that may leave its block BEFORE it runs, at runtime.
bool mayExitBefore(uint8_t op) {
    if (!c_hasIo) return false;
    const uint8_t k = s_kind[op];
    return isIndexed(s_mode[op]) && (isReadKind(k) || isWriteKind(k) || isRmwKind(k));
}

// --------------------------------------------------------------------------
// Emitter. Inserts a nop after a load only when the next instruction reads the
// loaded register (R3000 has no load interlock). Every branch and jump gets a
// nop delay slot unless a caller supplies one explicitly.

struct Emitter {
    uint32_t* code;
    uint32_t pos = 0;
    int lastLoad = -1;

    static uint32_t m(int r) { return r ? (1u << r) : 0; }
    void raw(uint32_t w) {
        code[pos++] = w;
        lastLoad = -1;
    }
    void emit(uint32_t w, uint32_t reads, int loadDst = -1) {
        if (lastLoad > 0 && (reads & (1u << lastLoad))) code[pos++] = 0;
        code[pos++] = w;
        lastLoad = loadDst;
    }
    void nop() { raw(0); }
    void settle() {
        if (lastLoad > 0) raw(0);
    }
    uint32_t here() {
        settle();
        return pos;
    }

    void R(uint32_t fn, int rs, int rt, int rd, int sh = 0) {
        emit((rs << 21) | (rt << 16) | (rd << 11) | (sh << 6) | fn, m(rs) | m(rt));
    }
    void addu(int d, int s, int t) { R(0x21, s, t, d); }
    void subu(int d, int s, int t) { R(0x23, s, t, d); }
    void and_(int d, int s, int t) { R(0x24, s, t, d); }
    void or_(int d, int s, int t) { R(0x25, s, t, d); }
    void xor_(int d, int s, int t) { R(0x26, s, t, d); }
    void nor(int d, int s, int t) { R(0x27, s, t, d); }
    void sltu(int d, int s, int t) { R(0x2b, s, t, d); }
    void sll(int d, int t, int sa) { R(0x00, 0, t, d, sa); }
    void srl(int d, int t, int sa) { R(0x02, 0, t, d, sa); }
    void srlv(int d, int t, int s) { R(0x06, s, t, d); }
    void move(int d, int s) { addu(d, s, ZERO); }

    void I(uint32_t op, int rs, int rt, int32_t imm) {
        emit((op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff), m(rs));
    }
    void addiu(int t, int s, int32_t imm) { I(0x09, s, t, imm); }
    void sltiu(int t, int s, int32_t imm) { I(0x0b, s, t, imm); }
    void andi(int t, int s, uint32_t imm) { I(0x0c, s, t, imm); }
    void ori(int t, int s, uint32_t imm) { I(0x0d, s, t, imm); }
    void xori(int t, int s, uint32_t imm) { I(0x0e, s, t, imm); }
    void lui(int t, uint32_t imm) { I(0x0f, 0, t, imm); }
    void li(int t, uint32_t v) {
        if (v <= 0xffff) {
            ori(t, ZERO, v);
        } else {
            lui(t, v >> 16);
            if (v & 0xffff) ori(t, t, v & 0xffff);
        }
    }
    void load(uint32_t op, int t, int32_t off, int base) {
        emit((op << 26) | (base << 21) | (t << 16) | (off & 0xffff), m(base), t);
    }
    void lbu(int t, int32_t off, int base) { load(0x24, t, off, base); }
    void lhu(int t, int32_t off, int base) { load(0x25, t, off, base); }
    void lw(int t, int32_t off, int base) { load(0x23, t, off, base); }
    void store(uint32_t op, int t, int32_t off, int base) {
        emit((op << 26) | (base << 21) | (t << 16) | (off & 0xffff), m(base) | m(t));
    }
    void sb(int t, int32_t off, int base) { store(0x28, t, off, base); }
    void sh(int t, int32_t off, int base) { store(0x29, t, off, base); }
    void sw(int t, int32_t off, int base) { store(0x2b, t, off, base); }

    // Branches return the index of the branch word, for patch(). The delay slot
    // is a nop.
    uint32_t brI(uint32_t op, int rs, int rt, uint32_t reads) {
        emit((op << 26) | (rs << 21) | (rt << 16), reads);
        uint32_t at = pos - 1;
        nop();
        return at;
    }
    uint32_t beq(int s, int t) { return brI(0x04, s, t, m(s) | m(t)); }
    uint32_t bne(int s, int t) { return brI(0x05, s, t, m(s) | m(t)); }
    uint32_t bltz(int s) { return brI(0x01, s, 0, m(s)); }
    void patch(uint32_t at, uint32_t target) {
        int32_t off = (int32_t)target - (int32_t)(at + 1);
        code[at] = (code[at] & 0xffff0000) | (off & 0xffff);
    }
    void j(const void* addr) {
        emit((0x02u << 26) | ((((uint32_t)addr) >> 2) & 0x3ffffff), 0);
        nop();
    }
    void jr(int s) {
        emit((s << 21) | 0x08, m(s));
        nop();
    }
};

// --------------------------------------------------------------------------

constexpr int32_t memOff(uint32_t ea) { return (int32_t)ea - 0x8000; }

// Where N and Z currently live, decided at translation time. Materializing into
// rZ/rN is deferred until an exit needs it or another op writes them directly.
enum NzLoc : uint8_t { NZ_MAT, NZ_A, NZ_X, NZ_Y };

int nzRegOf(NzLoc l) {
    switch (l) {
        case NZ_A:
            return rA;
        case NZ_X:
            return rX;
        case NZ_Y:
            return rY;
        default:
            return rZ;
    }
}

void materialize(Emitter& e, NzLoc l) {
    if (l == NZ_MAT) return;
    int r = nzRegOf(l);
    e.move(rZ, r);
    e.move(rN, r);
}

void exitWith(Emitter& e, uint32_t pc, uint32_t reason) {
    e.ori(V0, ZERO, pc);
    e.ori(A1, ZERO, reason);
    e.j(s_exitCommon);
}

// Jump to the block for a static guest pc, or exit to the dispatcher with it.
void chainStatic(Emitter& e, uint32_t target) {
    uint32_t addr = (uint32_t)&s_table[target];
    e.lui(T0, (addr + 0x8000) >> 16);
    e.lw(T0, (int16_t)(addr & 0xffff), T0);
    e.ori(V0, ZERO, target);
    uint32_t b = e.beq(T0, ZERO);
    e.jr(T0);
    e.patch(b, e.here());
    e.ori(A1, ZERO, EXIT_CHAIN);
    e.j(s_exitCommon);
}

// Same with the guest pc in a register.
void chainDynamic(Emitter& e, int r) {
    e.sll(T0, r, 2);
    e.addu(T0, T0, rTAB);
    e.lw(T0, 0, T0);
    e.move(V0, r);
    uint32_t b = e.beq(T0, ZERO);
    e.jr(T0);
    e.patch(b, e.here());
    e.ori(A1, ZERO, EXIT_CHAIN);
    e.j(s_exitCommon);
}

void emitTrampolines() {
    Emitter e;
    e.code = s_arena;

    // enter(a0 = code, a1 = state)
    s_enter = (EnterFn)&s_arena[e.here()];
    e.addiu(SP, SP, -48);
    e.sw(RA, 44, SP);
    e.sw(FP, 40, SP);
    e.sw(S7, 36, SP);
    e.sw(S6, 32, SP);
    e.sw(S5, 28, SP);
    e.sw(S4, 24, SP);
    e.sw(S3, 20, SP);
    e.sw(S2, 16, SP);
    e.sw(S1, 12, SP);
    e.sw(S0, 8, SP);
    e.move(rST, A1);
    e.lbu(rA, offsetof(State, a), rST);
    e.lbu(rX, offsetof(State, x), rST);
    e.lbu(rY, offsetof(State, y), rST);
    e.lbu(rS, offsetof(State, s), rST);
    e.lbu(rZ, offsetof(State, nz), rST);
    e.lbu(rN, offsetof(State, n), rST);
    e.lbu(rC, offsetof(State, c), rST);
    e.lbu(rV, offsetof(State, v), rST);
    e.move(T7, A0);
    e.lbu(T0, offsetof(State, d), rST);
    e.lbu(T1, offsetof(State, i), rST);
    e.sll(T0, T0, 3);
    e.sll(T1, T1, 2);
    e.or_(rP, T0, T1);
    e.ori(rP, rP, 0x30);
    e.lw(rCYC, offsetof(State, budget), rST);
    e.lw(rBITS, offsetof(State, codeBits), rST);
    e.lw(rTAB, offsetof(State, table), rST);
    e.lw(rMEM, offsetof(State, mem), rST);
    e.ori(AT, ZERO, 0x8000);
    e.addu(rMEM, rMEM, AT);
    e.jr(T7);

    // exitCommon(v0 = guest pc, a1 = reason)
    s_exitCommon = &s_arena[e.here()];
    e.sb(rA, offsetof(State, a), rST);
    e.sb(rX, offsetof(State, x), rST);
    e.sb(rY, offsetof(State, y), rST);
    e.sb(rS, offsetof(State, s), rST);
    e.sb(rZ, offsetof(State, nz), rST);
    e.sb(rN, offsetof(State, n), rST);
    e.sb(rC, offsetof(State, c), rST);
    e.sb(rV, offsetof(State, v), rST);
    e.srl(T0, rP, 3);
    e.andi(T0, T0, 1);
    e.sb(T0, offsetof(State, d), rST);
    e.srl(T0, rP, 2);
    e.andi(T0, T0, 1);
    e.sb(T0, offsetof(State, i), rST);
    e.sh(V0, offsetof(State, pc), rST);
    e.sw(rCYC, offsetof(State, budget), rST);
    e.sw(A1, offsetof(State, reason), rST);
    e.lw(S0, 8, SP);
    e.lw(S1, 12, SP);
    e.lw(S2, 16, SP);
    e.lw(S3, 20, SP);
    e.lw(S4, 24, SP);
    e.lw(S5, 28, SP);
    e.lw(S6, 32, SP);
    e.lw(S7, 36, SP);
    e.lw(FP, 40, SP);
    e.lw(RA, 44, SP);
    e.addiu(SP, SP, 48);
    e.jr(RA);

    s_arenaBase = (e.pos + 3) & ~3u;
    s_arenaUsed = s_arenaBase;
}

// --------------------------------------------------------------------------
// Block translation.

struct Insn {
    uint32_t pc;
    uint32_t operand;
    uint8_t op, len;
};

enum StubKind : uint8_t { ST_SIDE, ST_SMC_CONST, ST_SMC_DYN, ST_SMC_STACK, ST_SLOW, ST_IRQ };

struct Stub {
    uint32_t branchAt;
    uint32_t pc;  // guest pc to continue at
    int32_t adj;  // cycles to add back to the budget
    NzLoc nz;
    uint8_t kind;
    bool trap;
    uint32_t ea;
};

struct Translator {
    Emitter e;
    State* st;
    uint8_t* mem;
    Insn ins[c_maxInsns];
    uint8_t liveAfter[c_maxInsns];
    int32_t suffix[c_maxInsns];
    Stub stubs[c_maxInsns * 3 + 4];
    int numStubs = 0;
    NzLoc nz = NZ_MAT;
    int cur = 0;
    int n = 0;
    bool dec = false;     // translating for D = 1
    bool decAfter[c_maxInsns + 1];  // an ADC/SBC exists at index >= i

    // After an instruction that may change D, leave the block if D no longer
    // matches what this translation assumed and an ADC/SBC is still ahead.
    void dGuard() {
        if (!decAfter[cur + 1]) return;
        e.andi(T0, rP, 0x08);
        uint32_t b = dec ? e.beq(T0, ZERO) : e.bne(T0, ZERO);
        addStub(b, ST_SIDE, nextPc(), suffix[cur]);
    }

    void adcDecimal(int m) {
        e.andi(T4, rA, 0xf);
        e.andi(T5, m, 0xf);
        e.addu(T4, T4, T5);
        e.addu(T4, T4, rC);  // lo
        e.andi(T5, rA, 0xf0);
        e.andi(T6, m, 0xf0);
        e.addu(T5, T5, T6);  // hi
        e.sltiu(T6, T4, 10);
        uint32_t b1 = e.bne(T6, ZERO);
        e.addiu(T4, T4, 6);
        e.patch(b1, e.here());
        e.sltiu(T6, T4, 0x10);
        uint32_t b2 = e.bne(T6, ZERO);
        e.addiu(T5, T5, 0x10);
        e.patch(b2, e.here());
        e.addu(T7, rA, m);
        e.addu(T7, T7, rC);
        e.andi(rZ, T7, 0xff);  // Z from the binary sum
        if (vLive()) {
            e.xor_(T6, rA, m);
            e.nor(T6, T6, ZERO);
            e.xor_(T7, rA, T5);
            e.and_(T6, T6, T7);
            e.srl(rV, T6, 7);
            e.andi(rV, rV, 1);
        }
        e.sltiu(T6, T5, 0x91);
        uint32_t b3 = e.bne(T6, ZERO);
        e.addiu(T5, T5, 0x60);
        e.patch(b3, e.here());
        e.sltiu(T6, T5, 0x100);
        e.xori(rC, T6, 1);
        e.move(rN, T5);
        e.andi(T4, T4, 0xf);
        e.or_(T4, T4, T5);
        e.andi(rA, T4, 0xff);
        nz = NZ_MAT;
    }

    void sbcDecimal(int m) {
        e.xori(T6, rC, 1);  // borrow
        e.subu(T7, rA, m);
        e.subu(T7, T7, T6);  // binary result
        if (vLive()) {
            e.xor_(T4, rA, m);
            e.xor_(T5, rA, T7);
            e.and_(T4, T4, T5);
            e.srl(rV, T4, 7);
            e.andi(rV, rV, 1);
        }
        e.andi(T4, rA, 0xf);
        e.andi(T5, m, 0xf);
        e.subu(T4, T4, T5);
        e.subu(T4, T4, T6);  // lo, signed
        e.andi(T5, rA, 0xf0);
        e.andi(AT, m, 0xf0);
        e.subu(T5, T5, AT);  // hi, signed
        uint32_t b1 = e.brI(0x01, T4, 1, Emitter::m(T4));  // bgez
        e.addiu(T4, T4, -6);
        e.addiu(T5, T5, -0x10);
        e.patch(b1, e.here());
        uint32_t b2 = e.brI(0x01, T5, 1, Emitter::m(T5));  // bgez
        e.addiu(T5, T5, -0x60);
        e.patch(b2, e.here());
        e.srl(T6, T7, 8);
        e.sltiu(rC, T6, 1);
        e.andi(rZ, T7, 0xff);
        e.move(rN, rZ);
        e.andi(T4, T4, 0xf);
        e.andi(T5, T5, 0xf0);
        e.or_(rA, T4, T5);
        nz = NZ_MAT;
    }

    Stub& addStub(uint32_t branchAt, uint8_t kind, uint32_t pc, int32_t adj) {
        Stub& s = stubs[numStubs++];
        s.branchAt = branchAt;
        s.kind = kind;
        s.pc = pc;
        s.adj = adj;
        s.nz = nz;
        s.trap = false;
        s.ea = 0;
        return s;
    }

    bool cLive() const { return liveAfter[cur] & 1; }
    bool vLive() const { return liveAfter[cur] & 2; }
    uint32_t nextPc() const { return (ins[cur].pc + ins[cur].len) & 0xffff; }

    // Leave before this instruction, for the interpreter to run it.
    void slowStub(uint32_t b) {
        addStub(b, ST_SLOW, ins[cur].pc, suffix[cur] + c_cycles[ins[cur].op]);
    }

    // The effective address in T2 may be I/O: leave before reading it.
    void ioCheck() {
        e.srl(T0, T2, 12);
        e.xori(T0, T0, 0xd);
        slowStub(e.beq(T0, ZERO));
    }

    // The effective address in T2 may not be plain RAM: leave before writing.
    void writeCheck() {
        const Insn& in = ins[cur];
        const uint8_t mode = s_mode[in.op];
        if (mode == M_ZPX || mode == M_ZPY) {
            e.sltiu(T0, T2, 2);
            slowStub(e.bne(T0, ZERO));
            return;
        }
        if ((mode == M_ABSX || mode == M_ABSY) && !anyInRange(in.operand, [](uint32_t a) { return !JBus::direct(a); })) {
            return;
        }
        // Slow regions $A000-$BFFF and $D000-$FFFF as a 16-bit mask over the
        // top nibble, plus $00/$01.
        e.srl(T0, T2, 12);
        e.ori(T1, ZERO, 0xec00);
        e.srlv(T1, T1, T0);
        e.sltiu(T0, T2, 2);
        e.or_(T1, T1, T0);
        e.andi(T1, T1, 1);
        slowStub(e.bne(T1, ZERO));
    }

    void indyPenalty() {
        e.andi(T5, T4, 0xff);
        e.addu(T5, T5, rY);
        e.srl(T5, T5, 8);
        e.subu(rCYC, rCYC, T5);
    }

    // Load the operand byte of a read instruction into dst. Handles the dynamic
    // page-crossing penalty.
    void loadOperand(int dst) {
        const Insn& in = ins[cur];
        const uint8_t mode = s_mode[in.op];
        const uint32_t v = in.operand;
        switch (mode) {
            case M_IMM:
                e.ori(dst, ZERO, v);
                break;
            case M_ZP:
            case M_ABS:
                e.lbu(dst, memOff(v), rMEM);
                break;
            case M_ZPX:
            case M_ZPY:
                e.addiu(T2, mode == M_ZPX ? rX : rY, v);
                e.andi(T2, T2, 0xff);
                e.addu(T1, T2, rMEM);
                e.lbu(dst, memOff(0), T1);
                break;
            case M_ABSX:
            case M_ABSY: {
                int r = mode == M_ABSX ? rX : rY;
                if (c_hasIo && anyInRange(v, JBus::isIo)) {
                    e.ori(T2, ZERO, v);
                    e.addu(T2, T2, r);
                    if (v > 0xff00) e.andi(T2, T2, 0xffff);
                    ioCheck();
                    e.addu(T1, T2, rMEM);
                    e.lbu(dst, memOff(0), T1);
                } else if (v <= 0xff00) {
                    e.addu(T1, r, rMEM);
                    e.lbu(dst, memOff(v), T1);
                } else {
                    e.ori(T2, ZERO, v);
                    e.addu(T2, T2, r);
                    e.andi(T2, T2, 0xffff);
                    e.addu(T1, T2, rMEM);
                    e.lbu(dst, memOff(0), T1);
                }
                e.addiu(T5, r, v & 0xff);
                e.srl(T5, T5, 8);
                e.subu(rCYC, rCYC, T5);
                break;
            }
            case M_INDX:
                indxAddr(v);
                if (c_hasIo) ioCheck();
                e.addu(T1, T2, rMEM);
                e.lbu(dst, memOff(0), T1);
                break;
            case M_INDY:
                indyAddr(v, !c_hasIo);
                if (c_hasIo) {
                    ioCheck();
                    indyPenalty();
                }
                e.addu(T1, T2, rMEM);
                e.lbu(dst, memOff(0), T1);
                break;
        }
    }

    void indxAddr(uint32_t zp) {
        e.addiu(T2, rX, zp);
        e.andi(T2, T2, 0xff);
        e.addu(T1, T2, rMEM);
        e.lbu(T4, memOff(0), T1);
        e.addiu(T2, T2, 1);
        e.andi(T2, T2, 0xff);
        e.addu(T1, T2, rMEM);
        e.lbu(T5, memOff(0), T1);
        e.sll(T5, T5, 8);
        e.or_(T2, T4, T5);
    }

    void indyAddr(uint32_t zp, bool penalty) {
        e.lbu(T4, memOff(zp), rMEM);
        e.lbu(T5, memOff((zp + 1) & 0xff), rMEM);
        e.sll(T5, T5, 8);
        e.or_(T4, T4, T5);
        e.addu(T2, T4, rY);
        e.andi(T2, T2, 0xffff);
        if (penalty) indyPenalty();
    }

    // Compute the effective address of a write (or read-modify-write). Returns
    // true and sets ea for a constant address; false leaves it in T2.
    bool writeAddr(uint32_t& ea) {
        const Insn& in = ins[cur];
        const uint8_t mode = s_mode[in.op];
        const uint32_t v = in.operand;
        switch (mode) {
            case M_ZP:
            case M_ABS:
                ea = v;
                return true;
            case M_ZPX:
            case M_ZPY:
                e.addiu(T2, mode == M_ZPX ? rX : rY, v);
                e.andi(T2, T2, 0xff);
                return false;
            case M_ABSX:
            case M_ABSY:
                e.ori(T2, ZERO, v);
                e.addu(T2, T2, mode == M_ABSX ? rX : rY);
                if (v > 0xff00) e.andi(T2, T2, 0xffff);
                return false;
            case M_INDX:
                indxAddr(v);
                return false;
            case M_INDY:
                indyAddr(v, false);
                return false;
        }
        ea = 0;
        return true;
    }

    void loadAt(int dst, bool isConst, uint32_t ea) {
        if (isConst) {
            e.lbu(dst, memOff(ea), rMEM);
        } else {
            e.addu(T1, T2, rMEM);
            e.lbu(dst, memOff(0), T1);
        }
    }

    // Store, then check the code bitmap. Must be the last thing an instruction
    // does, since a hit exits to the next instruction.
    void storeAt(int val, bool isConst, uint32_t ea) {
        if (isConst) {
            if (ea < 0x200) {
                // Pages 0 and 1 are never translated, so nothing to invalidate.
                e.sb(val, memOff(ea), rMEM);
                return;
            }
            e.lbu(T0, ea >> 3, rBITS);
            e.sb(val, memOff(ea), rMEM);
            e.andi(T0, T0, 1u << (ea & 7));
            uint32_t b = e.bne(T0, ZERO);
            addStub(b, ST_SMC_CONST, nextPc(), suffix[cur]).ea = ea;
        } else {
            const uint8_t mode = s_mode[ins[cur].op];
            e.addu(T1, T2, rMEM);
            e.sb(val, memOff(0), T1);
            if (mode == M_ZPX || mode == M_ZPY) return;
            e.srl(T0, T2, 3);
            e.addu(T0, T0, rBITS);
            e.lbu(T0, 0, T0);
            e.andi(T1, T2, 7);
            e.srlv(T0, T0, T1);
            e.andi(T0, T0, 1);
            uint32_t b = e.bne(T0, ZERO);
            addStub(b, ST_SMC_DYN, nextPc(), suffix[cur]);
        }
    }

    // Push a byte without checking; caller checks the stack page afterwards.
    void pushNoCheck(int val) {
        e.addu(T1, rS, rMEM);
        e.sb(val, memOff(0x100), T1);
        e.addiu(rS, rS, -1);
        e.andi(rS, rS, 0xff);
    }
    // Page 1 is never translated, so a push cannot hit translated code.
    void stackCheck(uint32_t) {}
    void pull(int dst) {
        e.addiu(rS, rS, 1);
        e.andi(rS, rS, 0xff);
        e.addu(T1, rS, rMEM);
        e.lbu(dst, memOff(0x100), T1);
    }

    void packP(int dst) {
        int zr = nzRegOf(nz), nr = nz == NZ_MAT ? rN : nzRegOf(nz);
        e.andi(dst, nr, 0x80);
        e.sltiu(T5, zr, 1);
        e.sll(T5, T5, 1);
        e.or_(dst, dst, T5);
        e.or_(dst, dst, rC);
        e.sll(T5, rV, 6);
        e.or_(dst, dst, T5);
        e.or_(dst, dst, rP);
    }
    void unpackP(int src) {
        e.andi(rC, src, 1);
        e.andi(T5, src, 0x0c);
        e.ori(rP, T5, 0x30);
        e.srl(rV, src, 6);
        e.andi(rV, rV, 1);
        e.move(rN, src);
        e.nor(T5, src, ZERO);
        e.andi(rZ, T5, 2);
        nz = NZ_MAT;
    }

    void adc(int m) {
        e.addu(T4, rA, m);
        e.addu(T4, T4, rC);
        if (vLive()) {
            e.xor_(T5, rA, m);
            e.nor(T5, T5, ZERO);
            e.xor_(T6, rA, T4);
            e.and_(T5, T5, T6);
            e.srl(rV, T5, 7);
            e.andi(rV, rV, 1);
        }
        if (cLive()) e.srl(rC, T4, 8);
        e.andi(rA, T4, 0xff);
        nz = NZ_A;
    }

    void compare(int reg) {
        const Insn& in = ins[cur];
        if (s_mode[in.op] == M_IMM) {
            if (cLive()) {
                e.sltiu(rC, reg, in.operand);
                e.xori(rC, rC, 1);
            }
            e.addiu(T4, reg, -(int32_t)in.operand);
        } else {
            loadOperand(T3);
            if (cLive()) {
                e.sltu(rC, reg, T3);
                e.xori(rC, rC, 1);
            }
            e.subu(T4, reg, T3);
        }
        e.andi(rZ, T4, 0xff);
        e.move(rN, rZ);
        nz = NZ_MAT;
    }

    // Read-modify-write on memory or A.
    void rmw(uint8_t kind) {
        const Insn& in = ins[cur];
        if (s_mode[in.op] == M_ACC) {
            switch (kind) {
                case K_ASL:
                    if (cLive()) e.srl(rC, rA, 7);
                    e.sll(rA, rA, 1);
                    e.andi(rA, rA, 0xff);
                    break;
                case K_LSR:
                    if (cLive()) e.andi(rC, rA, 1);
                    e.srl(rA, rA, 1);
                    break;
                case K_ROL:
                    e.sll(T4, rA, 1);
                    e.or_(T4, T4, rC);
                    if (cLive()) e.srl(rC, rA, 7);
                    e.andi(rA, T4, 0xff);
                    break;
                case K_ROR:
                    e.sll(T4, rC, 7);
                    if (cLive()) e.andi(rC, rA, 1);
                    e.srl(rA, rA, 1);
                    e.or_(rA, rA, T4);
                    break;
            }
            nz = NZ_A;
            return;
        }
        uint32_t ea;
        bool isConst = writeAddr(ea);
        if (c_hasIo && !isConst) writeCheck();
        loadAt(T3, isConst, ea);
        switch (kind) {
            case K_INC:
                e.addiu(T4, T3, 1);
                e.andi(T4, T4, 0xff);
                break;
            case K_DEC:
                e.addiu(T4, T3, -1);
                e.andi(T4, T4, 0xff);
                break;
            case K_ASL:
                e.sll(T4, T3, 1);
                e.andi(T4, T4, 0xff);
                if (cLive()) e.srl(rC, T3, 7);
                break;
            case K_LSR:
                e.srl(T4, T3, 1);
                if (cLive()) e.andi(rC, T3, 1);
                break;
            case K_ROL:
                e.sll(T4, T3, 1);
                e.or_(T4, T4, rC);
                e.andi(T4, T4, 0xff);
                if (cLive()) e.srl(rC, T3, 7);
                break;
            case K_ROR:
                e.sll(T4, rC, 7);
                e.srl(T5, T3, 1);
                e.or_(T4, T4, T5);
                if (cLive()) e.andi(rC, T3, 1);
                break;
        }
        e.move(rZ, T4);
        e.move(rN, T4);
        nz = NZ_MAT;
        storeAt(T4, isConst, ea);
    }

    void branch() {
        const Insn& in = ins[cur];
        const uint32_t next = nextPc();
        const uint32_t target = (next + (int8_t)in.operand) & 0xffff;
        uint32_t b;
        switch (in.op) {
            case 0x10:
            case 0x30: {
                int nr = nz == NZ_MAT ? rN : nzRegOf(nz);
                e.andi(T0, nr, 0x80);
                b = in.op == 0x30 ? e.bne(T0, ZERO) : e.beq(T0, ZERO);
                break;
            }
            case 0x50:
                b = e.beq(rV, ZERO);
                break;
            case 0x70:
                b = e.bne(rV, ZERO);
                break;
            case 0x90:
                b = e.beq(rC, ZERO);
                break;
            case 0xb0:
                b = e.bne(rC, ZERO);
                break;
            case 0xd0:
                b = e.bne(nzRegOf(nz), ZERO);
                break;
            default:  // 0xf0
                b = e.beq(nzRegOf(nz), ZERO);
                break;
        }
        if ((int8_t)in.operand == -2) {
            Stub& s = addStub(b, ST_SIDE, in.pc, suffix[cur]);
            s.trap = true;
        } else {
            int32_t penalty = 1 + (((next ^ target) >> 8) & 1);
            addStub(b, ST_SIDE, target, suffix[cur] - penalty);
        }
    }

    // Returns false if the instruction ended the block (emitted its own exit).
    bool insn() {
        const Insn& in = ins[cur];
        const uint8_t op = in.op;
        const uint8_t kind = s_kind[op];
        const bool imm = s_mode[op] == M_IMM;
        switch (kind) {
            case K_LDA:
                loadOperand(rA);
                nz = NZ_A;
                return true;
            case K_LDX:
                loadOperand(rX);
                nz = NZ_X;
                return true;
            case K_LDY:
                loadOperand(rY);
                nz = NZ_Y;
                return true;
            case K_STA:
            case K_STX:
            case K_STY: {
                uint32_t ea;
                bool isConst = writeAddr(ea);
                if (c_hasIo && !isConst) writeCheck();
                storeAt(kind == K_STA ? rA : kind == K_STX ? rX : rY, isConst, ea);
                return true;
            }
            case K_AND:
            case K_ORA:
            case K_EOR:
                if (imm) {
                    if (kind == K_AND) e.andi(rA, rA, in.operand);
                    if (kind == K_ORA) e.ori(rA, rA, in.operand);
                    if (kind == K_EOR) e.xori(rA, rA, in.operand);
                } else {
                    loadOperand(T3);
                    if (kind == K_AND) e.and_(rA, rA, T3);
                    if (kind == K_ORA) e.or_(rA, rA, T3);
                    if (kind == K_EOR) e.xor_(rA, rA, T3);
                }
                nz = NZ_A;
                return true;
            case K_ADC:
                loadOperand(T3);
                if (dec) {
                    adcDecimal(T3);
                } else {
                    adc(T3);
                }
                return true;
            case K_SBC:
                loadOperand(T3);
                if (dec) {
                    sbcDecimal(T3);
                } else {
                    e.xori(T3, T3, 0xff);
                    adc(T3);
                }
                return true;
            case K_CMP:
                compare(rA);
                return true;
            case K_CPX:
                compare(rX);
                return true;
            case K_CPY:
                compare(rY);
                return true;
            case K_BIT:
                loadOperand(T3);
                e.move(rN, T3);
                if (vLive()) {
                    e.srl(rV, T3, 6);
                    e.andi(rV, rV, 1);
                }
                e.and_(rZ, rA, T3);
                nz = NZ_MAT;
                return true;
            case K_INC:
            case K_DEC:
            case K_ASL:
            case K_LSR:
            case K_ROL:
            case K_ROR:
                rmw(kind);
                return true;
            default:
                break;
        }

        if (isBranch(op)) {
            branch();
            return true;
        }

        switch (op) {
            case 0xaa:
                e.move(rX, rA);
                nz = NZ_X;
                return true;
            case 0xa8:
                e.move(rY, rA);
                nz = NZ_Y;
                return true;
            case 0x8a:
                e.move(rA, rX);
                nz = NZ_A;
                return true;
            case 0x98:
                e.move(rA, rY);
                nz = NZ_A;
                return true;
            case 0xba:
                e.move(rX, rS);
                nz = NZ_X;
                return true;
            case 0x9a:
                e.move(rS, rX);
                return true;
            case 0xe8:
            case 0xca:
                e.addiu(rX, rX, op == 0xe8 ? 1 : -1);
                e.andi(rX, rX, 0xff);
                nz = NZ_X;
                return true;
            case 0xc8:
            case 0x88:
                e.addiu(rY, rY, op == 0xc8 ? 1 : -1);
                e.andi(rY, rY, 0xff);
                nz = NZ_Y;
                return true;
            case 0x48:
                pushNoCheck(rA);
                stackCheck(nextPc());
                return true;
            case 0x08:
                packP(T4);
                pushNoCheck(T4);
                stackCheck(nextPc());
                return true;
            case 0x68:
                pull(rA);
                nz = NZ_A;
                return true;
            case 0x18:
                e.move(rC, ZERO);
                return true;
            case 0x38:
                e.ori(rC, ZERO, 1);
                return true;
            case 0xb8:
                e.move(rV, ZERO);
                return true;
            case 0x58:
                e.andi(rP, rP, 0x3b);
                if (c_hasIo) {
                    materialize(e, nz);
                    exitWith(e, nextPc(), EXIT_IRQ);
                    return false;
                }
                return true;
            case 0x78:
                e.ori(rP, rP, 0x04);
                return true;
            case 0xea:
                return true;

            // Block terminators below.
            case 0x28:
                pull(T4);
                unpackP(T4);
                dGuard();
                if (c_hasIo) {
                    e.andi(T0, rP, 0x04);
                    addStub(e.beq(T0, ZERO), ST_IRQ, nextPc(), suffix[cur]);
                }
                return true;
            case 0xd8:
            case 0xf8:
                if (op == 0xf8) {
                    e.ori(rP, rP, 0x08);
                } else {
                    e.andi(rP, rP, 0x37);
                }
                dGuard();
                return true;
            case 0x4c:
                if (in.operand == in.pc) {
                    materialize(e, nz);
                    exitWith(e, in.pc, EXIT_TRAP);
                    return false;
                }
                materialize(e, nz);
                chainStatic(e, in.operand);
                return false;
            case 0x6c: {
                uint32_t p = in.operand;
                uint32_t p2 = (p & 0xff00) | ((p + 1) & 0xff);
                e.lbu(T4, memOff(p), rMEM);
                e.lbu(T5, memOff(p2), rMEM);
                e.sll(T5, T5, 8);
                e.or_(T4, T4, T5);
                materialize(e, nz);
                chainDynamic(e, T4);
                return false;
            }
            case 0x20: {
                uint32_t ret = (in.pc + 2) & 0xffff;
                e.ori(T4, ZERO, ret >> 8);
                pushNoCheck(T4);
                e.ori(T4, ZERO, ret & 0xff);
                pushNoCheck(T4);
                stackCheck(in.operand);
                materialize(e, nz);
                chainStatic(e, in.operand);
                return false;
            }
            case 0x60:
                pull(T4);
                pull(T5);
                e.sll(T5, T5, 8);
                e.or_(T4, T4, T5);
                e.addiu(T4, T4, 1);
                e.andi(T4, T4, 0xffff);
                materialize(e, nz);
                chainDynamic(e, T4);
                return false;
            case 0x40:
                pull(T4);
                unpackP(T4);
                pull(T4);
                pull(T5);
                e.sll(T5, T5, 8);
                e.or_(T4, T4, T5);
                if (c_hasIo) {
                    e.andi(T0, rP, 0x04);
                    uint32_t b = e.bne(T0, ZERO);
                    e.move(V0, T4);
                    e.ori(A1, ZERO, EXIT_IRQ);
                    e.j(s_exitCommon);
                    e.patch(b, e.here());
                }
                chainDynamic(e, T4);
                return false;
            case 0x00: {
                uint32_t ret = (in.pc + 2) & 0xffff;
                e.ori(T4, ZERO, ret >> 8);
                pushNoCheck(T4);
                e.ori(T4, ZERO, ret & 0xff);
                pushNoCheck(T4);
                packP(T4);
                pushNoCheck(T4);
                e.ori(rP, rP, 0x04);
                e.lbu(T6, memOff(0xfffe), rMEM);
                e.lbu(T5, memOff(0xffff), rMEM);
                e.sll(T5, T5, 8);
                e.or_(T6, T6, T5);
                // The SMC stub for a stack hit exits at the BRK vector, which it
                // reads again itself, so it only needs to know this is a BRK.
                {
                    e.lbu(T0, 8192, rBITS);
                    uint32_t b = e.bne(T0, ZERO);
                    Stub& s = addStub(b, ST_SMC_STACK, 0x10000, 0);
                    (void)s;
                }
                materialize(e, nz);
                chainDynamic(e, T6);
                return false;
            }
        }
        return true;
    }

    void emitStubs() {
        for (int i = 0; i < numStubs; i++) {
            Stub& s = stubs[i];
            e.patch(s.branchAt, e.here());
            materialize(e, s.nz);
            if (s.adj) e.addiu(rCYC, rCYC, s.adj);
            switch (s.kind) {
                case ST_SIDE:
                    if (s.trap) {
                        exitWith(e, s.pc, EXIT_TRAP);
                    } else {
                        chainStatic(e, s.pc);
                    }
                    break;
                case ST_SMC_CONST:
                    e.li(T1, s.ea + 1);
                    e.sw(T1, offsetof(State, dirty), rST);
                    exitWith(e, s.pc, EXIT_SMC);
                    break;
                case ST_SMC_DYN:
                    e.addiu(T1, T2, 1);
                    e.sw(T1, offsetof(State, dirty), rST);
                    exitWith(e, s.pc, EXIT_SMC);
                    break;
                case ST_SLOW:
                    exitWith(e, s.pc, EXIT_SLOW);
                    break;
                case ST_IRQ:
                    exitWith(e, s.pc, EXIT_IRQ);
                    break;
                case ST_SMC_STACK:
                    e.ori(T1, ZERO, 0x101);
                    e.sw(T1, offsetof(State, dirty), rST);
                    if (s.pc == 0x10000) {
                        e.move(V0, T6);
                        e.ori(A1, ZERO, EXIT_SMC);
                        e.j(s_exitCommon);
                    } else {
                        exitWith(e, s.pc, EXIT_SMC);
                    }
                    break;
            }
        }
    }
};

Translator s_tr;

void setBits(uint32_t start, uint32_t end) {
    for (uint32_t a = start; a < end; a++) s_codeBits[a >> 3] |= 1u << (a & 7);
}

void recomputeStackFlag() {
    uint8_t any = 0;
    for (uint32_t i = 0x100 >> 3; i < 0x200 >> 3; i++) any |= s_codeBits[i];
    s_codeBits[8192] = any ? 1 : 0;
}

void flushAll() {
    for (uint32_t i = 0; i < 65536; i++) s_table[i] = s_tableD[i] = nullptr;
    for (uint32_t i = 0; i < sizeof(s_codeBits); i++) s_codeBits[i] = 0;
    s_numBlocks = 0;
    s_arenaUsed = s_arenaBase;
    s_stats.flushes++;
}

// Kill every block that overlaps [lo, hi).
void killRange(uint32_t lo, uint32_t hi) {
    // Kill every block covering the range, remembering the union of their spans
    // so only those bits are cleared and rebuilt.
    uint32_t klo = 0x10000, khi = 0;
    uint32_t w = 0;
    for (uint32_t i = 0; i < s_numBlocks; i++) {
        Block& b = s_blocks[i];
        if (b.start < hi && b.end > lo) {
            (b.dec ? s_tableD : s_table)[b.start] = nullptr;
            s_hot[b.start] = 0;
            s_hotD[b.start] = 0;
            s_stats.blocksKilled++;
            if (b.start < klo) klo = b.start;
            if (b.end > khi) khi = b.end;
        } else {
            s_blocks[w++] = b;
        }
    }
    s_numBlocks = w;
    if (klo >= khi) return;
    for (uint32_t a = klo; a < khi; a++) s_codeBits[a >> 3] &= ~(1u << (a & 7));
    for (uint32_t i = 0; i < s_numBlocks; i++) {
        const Block& b = s_blocks[i];
        if (b.start < khi && b.end > klo) {
            setBits(b.start > klo ? b.start : klo, b.end < khi ? b.end : khi);
        }
    }
    recomputeStackFlag();
}

void invalidate(uint32_t ea) {
    uint32_t lo = ea, hi = ea + 1;
    if (ea >= 0x100 && ea < 0x200) {
        lo = 0x100;
        hi = 0x200;
    }
    s_stats.invalidations++;
    s_smcHist[ea >> 3] |= 1u << (ea & 7);
    killRange(lo, hi);
}

// Run exactly one instruction in the interpreter.
Stop stepOne(State& st) {
    s_stats.singleSteps++;
    s_stopReq = false;
    Stop w = m6502::run<false, true, SBus>(st, 1);
    if (w == Stop::Smc) {
        invalidate(st.dirty - 1);
        w = Stop::Budget;
    }
    return w;
}

// An instruction that always needs the interpreter, decided from its operand:
// a constant address that is I/O to read or not plain RAM to write.
bool staticSlow(const Insn& in) {
    if (!c_hasIo) return false;
    const uint8_t k = s_kind[in.op];
    const uint8_t mode = s_mode[in.op];
    if (in.op == 0x6c) return JBus::isIo(in.operand) || JBus::isIo((in.operand & 0xff00) | ((in.operand + 1) & 0xff));
    if (mode != M_ABS && mode != M_ZP) return false;
    if (isReadKind(k)) return JBus::isIo(in.operand);
    if (isWriteKind(k) || isRmwKind(k)) return !JBus::direct(in.operand);
    return false;
}

void* compile(State& st, uint32_t start, bool dec) {
    Translator& t = s_tr;
    uint8_t* mem = st.mem;

    // Pages 0 and 1 are never translated: that removes the code-map check from
    // every zero-page and stack store.
    if (start < 0x200) {
        s_stats.compileFailed++;
        return nullptr;
    }

    // Decode.
    int n = 0;
    uint32_t pc = start;
    bool hasDec = false;
    s_failSlow = false;
    while (n < c_maxInsns) {
        uint8_t op = mem[pc];
        if (s_kind[op] == K_ILL) break;
        uint8_t len = s_len[op];
        if (pc + len > 0x10000) break;
        if (smcTouched(pc, len)) break;
        // Code is never translated from the I/O page.
        if (c_hasIo && (JBus::isIo(pc) || JBus::isIo(pc + len - 1))) break;
        Insn& in = t.ins[n];
        in.pc = pc;
        in.op = op;
        in.len = len;
        in.operand = len == 2 ? mem[pc + 1] : len == 3 ? (mem[pc + 1] | (mem[pc + 2] << 8)) : 0;
        if (staticSlow(in)) {
            if (n == 0) s_failSlow = true;
            break;
        }
        n++;
        pc += len;
        if (s_kind[op] == K_ADC || s_kind[op] == K_SBC) hasDec = true;
        if (endsTranslation(op)) break;
    }
    if (n == 0) {
        s_stats.compileFailed++;
        return nullptr;
    }
    const uint32_t end = pc;
    t.n = n;
    t.dec = dec;
    t.decAfter[n] = false;
    for (int i = n - 1; i >= 0; i--) {
        const uint8_t k = s_kind[t.ins[i].op];
        t.decAfter[i] = t.decAfter[i + 1] || k == K_ADC || k == K_SBC;
    }
    // Only the ADC/SBC that run before any D-changing instruction depend on the
    // D seen at entry; later ones are covered by dGuard.
    bool entryDec = false;
    for (int i = 0; i < n; i++) {
        const uint8_t op = t.ins[i].op;
        const uint8_t k = s_kind[op];
        if (k == K_ADC || k == K_SBC) {
            entryDec = true;
            break;
        }
        if (changesD(op)) break;
    }
    (void)hasDec;

    // Cycle totals.
    int32_t total = 0;
    for (int i = n - 1; i >= 0; i--) {
        t.suffix[i] = total;
        total += c_cycles[t.ins[i].op];
    }

    // C/V liveness. An exit reads both.
    uint8_t live = 3;
    for (int i = n - 1; i >= 0; i--) {
        uint8_t op = t.ins[i].op;
        uint8_t after = live | (mayExit(op) ? 3 : 0);
        if (i == n - 1) after = 3;
        t.liveAfter[i] = after;
        live = (after & ~flagWrites(op)) | flagReads(op);
        if (mayExitBefore(op)) live = 3;
    }

    // Room: generous worst case per instruction.
    const uint32_t need = (uint32_t)n * 96 + 64;
    if (s_arenaUsed + need > c_arenaWords || s_numBlocks >= c_maxBlocks) {
        flushAll();
    }

    t.e.code = &s_arena[s_arenaUsed];
    t.e.pos = 0;
    t.e.lastLoad = -1;
    t.numStubs = 0;
    t.nz = NZ_MAT;
    t.st = &st;
    t.mem = mem;
    Emitter& e = t.e;

    // Entry: D check (redirect to the D = 1 translation), then the budget. A
    // D = 1 translation is only ever entered through that redirect.
    uint32_t decBranch = 0;
    const bool entryCheck = entryDec && !dec;
    if (entryCheck) {
        e.andi(T0, rP, 0x08);
        decBranch = e.bne(T0, ZERO);
    }
    e.addiu(rCYC, rCYC, -total);
    uint32_t budBranch = e.bltz(rCYC);

    bool open = true;
    for (int i = 0; i < n && open; i++) {
        t.cur = i;
        open = t.insn();
    }
    if (open) {
        materialize(e, t.nz);
        chainStatic(e, end & 0xffff);
    }

    if (entryCheck) {
        e.patch(decBranch, e.here());
        uint32_t addr = (uint32_t)&s_tableD[start];
        e.lui(T0, (addr + 0x8000) >> 16);
        e.lw(T0, (int16_t)(addr & 0xffff), T0);
        e.ori(V0, ZERO, start);
        uint32_t miss = e.beq(T0, ZERO);
        e.jr(T0);
        e.patch(miss, e.here());
        e.ori(A1, ZERO, EXIT_DECIMAL);
        e.j(s_exitCommon);
    }
    e.patch(budBranch, e.here());
    e.addiu(rCYC, rCYC, total);
    exitWith(e, start, EXIT_BUDGET);

    t.emitStubs();
    e.settle();

    void* code = &s_arena[s_arenaUsed];
    s_arenaUsed += (e.pos + 3) & ~3u;
    s_stats.codeWords += e.pos;
    s_stats.compiled++;

    Block& b = s_blocks[s_numBlocks++];
    b.start = start;
    b.end = end;
    b.dec = dec;
    setBits(start, end);
    recomputeStackFlag();
    (dec ? s_tableD : s_table)[start] = code;
#ifdef JIT_DUMP
    if (start == JIT_DUMP) {
        ramsyscall_printf("DUMP %04x dec=%d n=%d words=%u\n", start, dec ? 1 : 0, n, e.pos);
        for (uint32_t i = 0; i < e.pos; i++) ramsyscall_printf("W %08x\n", ((uint32_t*)code)[i]);
    }
#endif

    syscall_flushCache();
    return code;
}

}  // namespace

void m6502jit::init(State& st) {
    for (unsigned i = 0; i < 256; i++) {
        s_kind[i] = K_ILL;
        s_mode[i] = M_IMP;
        s_len[i] = 0;
    }
    for (const auto& d : c_ops) {
        s_kind[d.op] = d.kind;
        s_mode[d.op] = d.mode;
        s_len[d.op] = modeLen(d.mode);
    }
    emitTrampolines();
    flushAll();
    s_stats = {};
    for (uint32_t i = 0; i < 65536; i++) s_hot[i] = s_hotD[i] = 0;
    for (uint32_t i = 0; i < 8192; i++) s_smcHist[i] = 0;
    st.codeBits = s_codeBits;
    st.table = s_table;
    syscall_flushCache();
}

const m6502jit::Stats& m6502jit::stats() { return s_stats; }

void m6502jit::invalidateRange(uint32_t lo, uint32_t hi) {
    s_stats.rangeFlushes++;
    killRange(lo, hi);
    for (uint32_t a = lo; a < hi; a++) s_hot[a] = s_hotD[a] = 0;
}

m6502::Stop m6502jit::run(State& st, uint32_t budget) {
    const uint32_t limit = st.cycles + budget;
    bool forceInterp = false;
    while ((int32_t)(limit - st.cycles) > 0) {
        const uint32_t pc = st.pc;
        void* b = forceInterp ? nullptr : s_table[pc];
        if (!b && !forceInterp && s_hot[pc] != c_noTranslate) {
            if (++s_hot[pc] >= c_threshold) {
                s_hot[pc] = 0;
                b = compile(st, pc, false);
                if (!b && s_failSlow) s_hot[pc] = c_noTranslate;
            }
        }
        forceInterp = false;
        if (b) {
            st.budget = (int32_t)(limit - st.cycles);
            s_enter(b, &st);
            st.cycles = limit - (uint32_t)st.budget;
            switch (st.reason) {
                case EXIT_CHAIN:
                    s_stats.exitsChain++;
                    break;
                case EXIT_BUDGET:
                    s_stats.exitsBudget++;
                    forceInterp = true;
                    break;
                case EXIT_DECIMAL:
                    s_stats.exitsDecimal++;
                    if (++s_hotD[st.pc] >= c_threshold) {
                        s_hotD[st.pc] = 0;
                        if (compile(st, st.pc, true)) break;
                    }
                    forceInterp = true;
                    break;
                case EXIT_SMC:
                    s_stats.exitsSmc++;
                    invalidate(st.dirty - 1);
                    break;
                case EXIT_TRAP:
                    return Stop::Trap;
                case EXIT_SLOW: {
                    s_stats.exitsSlow++;
                    const Stop w = stepOne(st);
                    if (w != Stop::Budget) return w;
                    if (s_stopReq) return Stop::Budget;
                    break;
                }
                case EXIT_IRQ:
                    s_stats.exitsIrq++;
                    if (!st.i && JBus::irqPending()) return Stop::Budget;
                    break;
            }
            continue;
        }
        if (s_hot[pc] == c_noTranslate) {
            const Stop w = stepOne(st);
            if (w != Stop::Budget) return w;
            if (s_stopReq) return Stop::Budget;
            continue;
        }
        s_stats.interpBlocks++;
        const uint32_t c0 = st.cycles;
        s_stopReq = false;
        Stop w = m6502::run<true, true, SBus>(st, limit - st.cycles);
        s_stats.interpCycles += st.cycles - c0;
        if (w == Stop::Smc) {
            invalidate(st.dirty - 1);
            if (s_stopReq) return Stop::Budget;
            continue;
        }
        if (w != Stop::Budget) return w;
        if (s_stopReq) return Stop::Budget;
    }
    return Stop::Budget;
}
