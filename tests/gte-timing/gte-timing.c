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

// GTE command execution time, as seen by the CPU when it reads a result.
//
// Timed with root counter 2 on the system clock, each block run twice so
// the second pass comes out of the instruction cache. Stalls are printed in
// hundredths of a cycle per repetition.
//
// Arms:
//   CMD  - 64 x "cop2 cmd; mfc2 IR1" against 64 x "nop; mfc2 IR1". The mfc2
//          waits for the command, so the difference is the command's time,
//          the same way tests/mult-timing reads mult latency.
//   GAP  - 8 x "cop2 cmd; N nops; mfc2 IR1" against the same with the
//          command replaced by a nop, N from 0 to 48: how the wait shrinks
//          as unrelated instructions fill it.
//   WHO  - 8 x "cop2 cmd; X; 64 nops" against 8 x "cop2 cmd; nop; 64 nops".
//          The 64 nops outlast any command, so the only thing X can add is
//          a stall of its own. X is each kind of cop2 access.
//   LZC  - 64 x "mtc2 LZCS; mfc2 LZCR" against 64 x "mtc2 VXY0; mfc2 LZCR".

#include <stdint.h>

#include "common/hardware/cop2.h"
#include "common/syscalls/syscalls.h"

#define T2_VALUE (*(volatile uint32_t *)0xbf801120)
#define T2_MODE (*(volatile uint32_t *)0xbf801124)

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}

static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

static uint32_t s_scratch[4];

// A block is `reps` copies of `body`. %[op] is the cop2 command, %[d] a
// scratch GPR, %[m] a pointer to scratch memory.
#define BLOCK(name, reps, opcode, body)                                              \
    static __attribute__((noinline)) uint32_t name(void) {                           \
        uint32_t t0, t1, d = 0;                                                      \
        __asm__ volatile(".set push\n.set noreorder\n"                               \
                         "lw %[t0], 0(%[t])\n"                                       \
                         ".rept " #reps "\n" body "\n.endr\n"                        \
                         "lw %[t1], 0(%[t])\n"                                       \
                         ".set pop\n"                                                \
                         : [t0] "=&r"(t0), [t1] "=&r"(t1), [d] "+r"(d)               \
                         : [t] "r"(&T2_VALUE), [m] "r"(s_scratch), [op] "i"(opcode) \
                         : "memory");                                                \
        return (t1 - t0) & 0xffff;                                                   \
    }

typedef uint32_t (*blockFn)(void);

static uint32_t measure(blockFn fn) {
    uint32_t sr = irqDisable();
    T2_MODE = 0;  // system clock, free running
    fn();
    uint32_t t = fn();
    irqRestore(sr);
    return t;
}

BLOCK(cmdBase, 64, 0, "nop\nmfc2 %[d], $9")
BLOCK(lzcBase, 64, 0, "mtc2 %[d], $0\nmfc2 %[d], $31")
BLOCK(lzcBlock, 64, 0, "mtc2 %[d], $30\nmfc2 %[d], $31")

// Gap values, and the functions for one tag at each of them.
#define GAPLIST(M, a, b)                                                                                  \
    M(a, b, 0) M(a, b, 1) M(a, b, 2) M(a, b, 3) M(a, b, 4) M(a, b, 6) M(a, b, 8) M(a, b, 12) M(a, b, 16) \
        M(a, b, 20) M(a, b, 24) M(a, b, 32) M(a, b, 40) M(a, b, 48)
#define GAPBLOCK(tag, opcode, N) \
    BLOCK(tag##_gap##N, 8, opcode, "cop2 %[op]\n.rept " #N "\nnop\n.endr\nmfc2 %[d], $9")
#define GAPBASE(tag, opcode, N) BLOCK(gapBase##N, 8, 0, "nop\n.rept " #N "\nnop\n.endr\nmfc2 %[d], $9")
#define GAPREF(tag, opcode, N) tag##_gap##N,
#define GAPREFB(tag, opcode, N) gapBase##N,
#define GAPNUM(tag, opcode, N) N,

GAPLIST(GAPBASE, x, 0)
static const int s_gapN[] = {GAPLIST(GAPNUM, x, 0)};
static const blockFn s_gapBase[] = {GAPLIST(GAPREFB, x, 0)};
#define NGAP (sizeof(s_gapN) / sizeof(s_gapN[0]))

#define WHO(X, tag, opcode)                                \
    X(tag, opcode, MfcRes, "mfc2 IR1", "mfc2 %[d], $9")    \
    X(tag, opcode, MfcIn, "mfc2 VXY0", "mfc2 %[d], $0")    \
    X(tag, opcode, CfcFlag, "cfc2 FLAG", "cfc2 %[d], $31") \
    X(tag, opcode, CfcMat, "cfc2 R11R12", "cfc2 %[d], $0") \
    X(tag, opcode, Swc2, "swc2 IR1", "swc2 $9, 0(%[m])")   \
    X(tag, opcode, Lwc2, "lwc2 VXY0", "lwc2 $0, 4(%[m])")  \
    X(tag, opcode, Mtc2, "mtc2 VXY0", "mtc2 %[d], $0")     \
    X(tag, opcode, Ctc2, "ctc2 ZSF3", "ctc2 %[d], $29")    \
    X(tag, opcode, Cop2, "cop2 NCLIP", "cop2 0x1400006")
#define WHOBLOCK(tag, opcode, suffix, label, insn) \
    BLOCK(tag##_w##suffix, 8, opcode, "cop2 %[op]\n" insn "\n.rept 64\nnop\n.endr")
#define WHOREF(tag, opcode, suffix, label, insn) tag##_w##suffix,
#define WHONAME(tag, opcode, suffix, label, insn) label,
#define NWHO 9

static const char *const s_whoNames[NWHO] = {WHO(WHONAME, x, 0)};

// Every documented command, flags as in Sony's conventional encodings, and
// MVMVA once more with the garbage matrix and the far colour vector.
#define COMMANDS(X)                                                    \
    X(RTPS, COP2_RTPS(1, 0))                                           \
    X(RTPT, COP2_RTPT(1, 0))                                           \
    X(NCLIP, COP2_NCLIP)                                               \
    X(OP, COP2_OP_CP(1, 0))                                            \
    X(DPCS, COP2_DPCS(1, 0))                                           \
    X(DPCT, COP2_DPCT(1, 0))                                           \
    X(DCPL, COP2_DCPL(1, 0))                                           \
    X(INTPL, COP2_INTPL(1, 0))                                         \
    X(MVMVA_RT, COP2_MVMVA(1, COP2_MX_RT, COP2_V_V0, COP2_CV_TR, 0))   \
    X(MVMVA_BAD, COP2_MVMVA(1, COP2_MX_BAD, COP2_V_IR, COP2_CV_FC, 1)) \
    X(NCS, COP2_NCS(1, 1))                                             \
    X(NCT, COP2_NCT(1, 1))                                             \
    X(NCCS, COP2_NCCS(1, 1))                                           \
    X(NCCT, COP2_NCCT(1, 1))                                           \
    X(NCDS, COP2_NCDS(1, 1))                                           \
    X(NCDT, COP2_NCDT(1, 1))                                           \
    X(CC, COP2_CC(1, 1))                                               \
    X(CDP, COP2_CDP(1, 1))                                             \
    X(SQR, COP2_SQR(1, 0))                                             \
    X(AVSZ3, COP2_AVSZ3)                                               \
    X(AVSZ4, COP2_AVSZ4)                                               \
    X(GPF, COP2_GPF(1, 0))                                             \
    X(GPL, COP2_GPL(1, 0))

#define DEFINE_BLOCKS(tag, opcode)                                         \
    BLOCK(tag##_cmd, 64, opcode, "cop2 %[op]\nmfc2 %[d], $9")              \
    BLOCK(tag##_wBase, 8, opcode, "cop2 %[op]\nnop\n.rept 64\nnop\n.endr") \
    WHO(WHOBLOCK, tag, opcode)                                             \
    GAPLIST(GAPBLOCK, tag, opcode)
COMMANDS(DEFINE_BLOCKS)

struct command {
    const char *name;
    blockFn cmd, wBase, w[NWHO], gaps[NGAP];
};

#define DEFINE_ENTRY(tag, opcode) \
    {#tag, tag##_cmd, tag##_wBase, {WHO(WHOREF, tag, opcode)}, {GAPLIST(GAPREF, tag, opcode)}},
static const struct command s_commands[] = {COMMANDS(DEFINE_ENTRY)};
#define NCOMMANDS (sizeof(s_commands) / sizeof(s_commands[0]))

static uint32_t s_cmdBase;

static void printStall(const char *arm, const char *name, const char *what, int n, uint32_t t, uint32_t base,
                       int reps) {
    int32_t s = (int32_t)(t - base) * 100 / reps;
    const char *sign = s < 0 ? "-" : "";
    if (s < 0) s = -s;
    ramsyscall_printf("%s %-10s %-12s n=%d total=%d base=%d stall=%s%d.%02d\n", arm, name, what, n, (int)t,
                      (int)base, sign, s / 100, s % 100);
}

static void gteSetup(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
    // A non-degenerate scene: identity-ish rotation, light and colour
    // matrices, a vertex off the axes, a projection plane, FIFOs filled.
    static const uint32_t ctrl[32] = {
        0x00000800, 0x00000000, 0x00000800, 0x00000000, 0x00000800, 0x00000010, 0x00000020, 0x00000400,
        0x07ff07ff, 0x07ff07ff, 0x07ff07ff, 0x07ff07ff, 0x000007ff, 0x00000100, 0x00000200, 0x00000300,
        0x08000800, 0x08000800, 0x08000800, 0x08000800, 0x00000800, 0x00000080, 0x00000040, 0x00000020,
        0x00a00000, 0x00780000, 0x000000c8, 0xfffff000, 0x00140000, 0x00000155, 0x00000100, 0x00000000,
    };
    static const uint32_t data[31] = {
        0x08000600, 0x00000a00, 0x04000500, 0x00000900, 0x03000700, 0x00000b00, 0x80406020, 0x00000000,
        0x00000400, 0x00000300, 0x00000200, 0x00000100, 0x00200010, 0x00400030, 0x00600050, 0x00600050,
        0x00000100, 0x00000200, 0x00000300, 0x00000400, 0x10203040, 0x50607080, 0x80402010, 0x00000000,
        0x00000100, 0x00000200, 0x00000300, 0x00000400, 0x00000000, 0x00000000, 0x12345678,
    };
    // Unrolled because mtc2/ctc2 take the register number as an immediate.
#define W(i) __asm__ volatile("mtc2 %0, $" #i "; nop; nop" : : "r"(data[i]));
#define C(i) __asm__ volatile("ctc2 %0, $" #i "; nop; nop" : : "r"(ctrl[i]));
    C(0) C(1) C(2) C(3) C(4) C(5) C(6) C(7) C(8) C(9) C(10) C(11) C(12) C(13) C(14) C(15)
    C(16) C(17) C(18) C(19) C(20) C(21) C(22) C(23) C(24) C(25) C(26) C(27) C(28) C(29) C(30)
    W(0) W(1) W(2) W(3) W(4) W(5) W(6) W(8) W(9) W(10) W(11) W(12) W(13) W(14) W(16) W(17) W(18)
    W(19) W(20) W(21) W(22) W(24) W(25) W(26) W(27) W(30)
#undef W
#undef C
}

int main(void) {
    ramsyscall_printf("GTE-TIMING-START\n");
    gteSetup();
    s_cmdBase = measure(cmdBase);
    ramsyscall_printf("BASE total=%d again=%d\n", (int)s_cmdBase, (int)measure(cmdBase));
    uint32_t gapBase[NGAP];
    for (unsigned g = 0; g < NGAP; g++) gapBase[g] = measure(s_gapBase[g]);

    for (unsigned c = 0; c < NCOMMANDS; c++) {
        const struct command *cmd = &s_commands[c];
        gteSetup();
        printStall("CMD", cmd->name, "mfc2 IR1", 0, measure(cmd->cmd), s_cmdBase, 64);
    }
    for (unsigned c = 0; c < NCOMMANDS; c++) {
        const struct command *cmd = &s_commands[c];
        gteSetup();
        uint32_t base = measure(cmd->wBase);
        for (unsigned w = 0; w < NWHO; w++)
            printStall("WHO", cmd->name, s_whoNames[w], 0, measure(cmd->w[w]), base, 8);
    }
    for (unsigned c = 0; c < NCOMMANDS; c++) {
        const struct command *cmd = &s_commands[c];
        gteSetup();
        for (unsigned g = 0; g < NGAP; g++)
            printStall("GAP", cmd->name, "mfc2 IR1", s_gapN[g], measure(cmd->gaps[g]), gapBase[g], 8);
    }
    gteSetup();
    uint32_t lzcBaseT = measure(lzcBase);
    printStall("LZC", "LZCS", "mfc2 LZCR", 0, measure(lzcBlock), lzcBaseT, 64);
    ramsyscall_printf("GTE-TIMING-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
