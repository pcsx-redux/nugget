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

/*
 * Which bits of cop0r12 (SR) exist on the console and what they do.
 *
 * 1. SR as the loader leaves it, and for each bit: write it alone, read
 *    back, write 0, read back.
 * 2. One arm per bit the R3000 manual gives a function to: CM after an
 *    isolated load hit and miss, PZ/PE on a store and reload, RE in user
 *    mode, CU0 in user mode, CU1/CU2/CU3 on coprocessor opcodes in kernel
 *    mode, IM8/IM9 with the Cause software interrupt bits, IM10 against
 *    I_STAT and I_MASK.
 *
 * Every arm prints what it observed; nothing is graded here. Interrupts stay
 * masked except inside the interrupt arms, and SR is restored after each arm.
 */

#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define ISTAT IREG

uint32_t sr_try(uint32_t value, uint32_t restore);
uint32_t cause_try(uint32_t value);
void cm_target(void);
void cm_probe(uint32_t hit, uint32_t miss, uint32_t sr, uint32_t out[6], uint32_t biu);
void bev_probe(uint32_t sr);
void pz_probe(uint32_t sr, volatile uint32_t *word, uint32_t out[2]);
void user_run(uint32_t data, uint32_t sr, uint32_t code);
void user_resume(void);
extern uint32_t ue_loads[], ue_sb[], ue_sh[], ue_mfc0[];

#define COPSTUBS(X)                                                                                \
    X(mfc0, 0) X(mfc1, 1) X(cfc1, 1) X(lwc1, 1) X(swc1, 1) X(cop1, 1) X(mfc2, 2) X(mfc3, 3) X(cfc3, 3) \
    X(lwc3, 3) X(swc3, 3) X(cop3, 3)
#define DECL(n, c) uint32_t cs_##n(volatile uint32_t *scratch);
COPSTUBS(DECL)

void installExceptionHandlers(uint32_t (*handler)(uint32_t *regs, uint32_t from));
void uninstallExceptionHandlers(void);

static inline uint32_t getSR(void) {
    uint32_t r;
    __asm__ volatile("mfc0 %0, $12; nop" : "=r"(r) : : "memory");
    return r;
}
static inline void setSR(uint32_t v) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(v) : "memory"); }
static inline uint32_t getCause(void) {
    uint32_t r;
    __asm__ volatile("mfc0 %0, $13; nop" : "=r"(r) : : "memory");
    return r;
}
static inline void setCause(uint32_t v) { __asm__ volatile("mtc0 %0, $13; nop; nop" : : "r"(v) : "memory"); }

enum { MODE_SKIP, MODE_RESUME, MODE_IRQ };

static volatile int s_mode;
static volatile int s_count;
static volatile uint32_t s_cause, s_epc, s_sr, s_bada, s_from;
static volatile uint32_t s_uregs[8];
static volatile uint32_t s_irqIStat;

static uint32_t handler(uint32_t *regs, uint32_t from) {
    uint32_t cause, epc, sr, bada;
    __asm__ volatile("mfc0 %0, $13; nop; mfc0 %1, $14; nop; mfc0 %2, $12; nop; mfc0 %3, $8; nop"
                     : "=r"(cause), "=r"(epc), "=r"(sr), "=r"(bada));
    if (s_count == 0) {
        s_cause = cause;
        s_epc = epc;
        s_sr = sr;
        s_bada = bada;
        s_from = from;
        for (unsigned i = 0; i < 8; i++) s_uregs[i] = regs[8 + i];
    }
    s_count++;
    switch (s_mode) {
        case MODE_RESUME:
            // Return to kernel: rfe pops KUp/IEp into the current bits.
            setSR(sr & ~0x0200003cu & ~0xf0000000u);
            return (uint32_t)user_resume;
        case MODE_IRQ:
            s_irqIStat = ISTAT;
            setCause(0);
            ISTAT = 0;
            if (s_count > 16) setSR(sr & ~0x0000ff04u);  // runaway guard: drop IEp and IM
            return epc;
        default:
            return epc + 4;
    }
}

static void resetExc(int mode) {
    s_mode = mode;
    s_count = 0;
    s_cause = s_epc = s_sr = s_bada = s_from = 0;
    for (unsigned i = 0; i < 8; i++) s_uregs[i] = 0;
}

static uint32_t s_base;  // SR used between arms: loader value, IEc clear

static void sweep(void) {
    typedef uint32_t (*try_t)(uint32_t, uint32_t);
    try_t kuseg = (try_t)((uint32_t)sr_try & 0x1fffffffu);
    uint32_t ones = 0, zeros = 0xffffffffu;
    for (unsigned b = 0; b < 32; b++) {
        uint32_t bit = 1u << b;
        uint32_t w1 = bit, r1, r0;
        try_t f = sr_try;
        if (b == 1) {
            // KUc: keep CU0 so the mfc0 is legal in user mode, run from KUSEG.
            w1 |= 1u << 28;
            f = kuseg;
        }
        r1 = f(w1, 0);
        r0 = sr_try(0, 0);
        setSR(s_base);
        if (r1 & bit) ones |= bit;
        if (!(r0 & bit)) zeros &= ~bit;
        ramsyscall_printf("SRBIT %2d wrote1=%08x read=%08x wrote0 read=%08x\n", b, w1, r1, r0);
    }
    ramsyscall_printf("SRMASK set1_reads1=%08x set0_reads1=%08x\n", ones, zeros);
    uint32_t c = cause_try(0xffffffffu);
    ramsyscall_printf("CAUSE wroteFFFFFFFF read=%08x\n", c);
}

static void cmArm(void) {
    uint32_t out[6];
    void (*probe)(uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t) =
        (void (*)(uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t))(((uint32_t)cm_probe & 0x1fffffffu) | 0xa0000000u);
    uint32_t hit = (uint32_t)cm_target;
    uint32_t miss = hit ^ 0x1000u;
    static const struct {
        const char *name;
        uint32_t biu, extra;
    } rows[] = {
        {"code", 0x0001e988u, 0},
        {"code+CM", 0x0001e988u, 0x00080000u},
        {"tag", 0x0001e90cu, 0},
        {"tag+CM", 0x0001e90cu, 0x00080000u},
        {"code+SwC", 0x0001e988u, 0x00020000u},
        {"code+SwC+CM", 0x0001e988u, 0x000a0000u},
    };
    for (unsigned r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        cm_target();  // fetch the line through the i-cache
        probe(hit, miss, (s_base & ~1u) | 0x00010000u | rows[r].extra, out, rows[r].biu);
        ramsyscall_printf(
            "CM %-11s biu=%08x hitAddr=%08x missAddr=%08x hit1=%08x sr=%08x miss=%08x sr=%08x hit2=%08x sr=%08x\n",
            rows[r].name, rows[r].biu, hit, miss, out[0], out[1], out[2], out[3], out[4], out[5]);
    }
}

static volatile uint32_t s_pzWord;

static void pzArm(void) {
    uint32_t out[2];
    for (unsigned v = 0; v < 2; v++) {
        uint32_t sr = (s_base & ~1u) | (v ? 0x00040000u : 0);
        s_pzWord = 0;
        resetExc(MODE_SKIP);
        pz_probe(sr, &s_pzWord, out);
        ramsyscall_printf("PZ pz=%d reload=%08x srAfter=%08x exc=%d\n", v, out[0], out[1], s_count);
    }
}

static volatile uint32_t s_userWord __attribute__((aligned(16)));

static void userArm(const char *name, uint32_t *block, uint32_t sr, int ksegCode, int ksegData, uint32_t init) {
    uint32_t code = (uint32_t)block;
    if (!ksegCode) code &= 0x1fffffffu;
    uint32_t data = (uint32_t)&s_userWord;
    if (!ksegData) data &= 0x1fffffffu;
    s_userWord = init;
    resetExc(MODE_RESUME);
    user_run(data, sr, code);
    setSR(s_base);
    ramsyscall_printf(
        "USER %-6s sr=%08x code=%08x exc=%d cause=%08x(exc %d ce %d) epc=%08x bada=%08x "
        "t0=%08x t1=%08x t2=%08x t3=%08x t4=%08x t5=%08x t6=%08x t7=%08x word=%08x\n",
        name, sr, code, s_count, s_cause, (s_cause >> 2) & 31, (s_cause >> 28) & 3, s_epc, s_bada, s_uregs[0],
        s_uregs[1], s_uregs[2], s_uregs[3], s_uregs[4], s_uregs[5], s_uregs[6], s_uregs[7], s_userWord);
}

static void userArms(void) {
    const uint32_t KUP = 1u << 3, RE = 1u << 25, CU0 = 1u << 28;
    uint32_t b = s_base & ~0x3fu & ~RE;
    // Kernel-mode reference: same blocks, KUp clear.
    userArm("loads", ue_loads, b, 0, 0, 0x11223344);
    userArm("loads", ue_loads, b | KUP, 0, 0, 0x11223344);
    userArm("loads", ue_loads, b | KUP | RE, 0, 0, 0x11223344);
    userArm("loads", ue_loads, b | RE, 0, 0, 0x11223344);
    userArm("sb", ue_sb, b, 0, 0, 0x11223344);
    userArm("sb", ue_sb, b | KUP, 0, 0, 0x11223344);
    userArm("sb", ue_sb, b | KUP | RE, 0, 0, 0x11223344);
    userArm("sh", ue_sh, b, 0, 0, 0x11223344);
    userArm("sh", ue_sh, b | KUP, 0, 0, 0x11223344);
    userArm("sh", ue_sh, b | KUP | RE, 0, 0, 0x11223344);
    // CU0 in user mode.
    userArm("mfc0", ue_mfc0, (b & ~CU0) | KUP, 0, 0, 0);
    userArm("mfc0", ue_mfc0, b | CU0 | KUP, 0, 0, 0);
    userArm("mfc0", ue_mfc0, b & ~CU0, 0, 0, 0);
    // User mode against KSEG0 data and KSEG0 code.
    userArm("kdata", ue_loads, b | KUP, 0, 1, 0x11223344);
    userArm("kcode", ue_loads, b | KUP, 1, 0, 0x11223344);
}

static volatile uint32_t s_scratch;

static void copArms(void) {
    struct {
        const char *name;
        uint32_t (*fn)(volatile uint32_t *);
        unsigned n;
    } stubs[] = {
#define ENT(n, c) {#n, cs_##n, c},
        COPSTUBS(ENT)
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); i++) {
        for (unsigned cu = 0; cu < 2; cu++) {
            unsigned n = stubs[i].n;
            uint32_t sr = (s_base & ~1u & ~0xf0000000u) | (cu ? (1u << (28 + n)) : 0);
            s_scratch = 0x5a5a5a5a;
            resetExc(MODE_SKIP);
            setSR(sr);
            uint32_t r = stubs[i].fn(&s_scratch);
            setSR(s_base);
            ramsyscall_printf("COP %-5s cu%d=%d t0=%08x exc=%d cause=%08x(exc %d ce %d) scratch=%08x\n",
                              stubs[i].name, n, cu, r, s_count, s_cause, (s_cause >> 2) & 31,
                              (s_cause >> 28) & 3, s_scratch);
        }
    }
}

static void delay(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++);
}

static void swIrqArms(void) {
    for (unsigned sw = 0; sw < 2; sw++) {
        uint32_t ip = 0x100u << sw;
        // IM set, IEc set: expect an interrupt.
        // IM clear, IEc set: expect none, IP latched.
        // IM set, IEc clear, then IEc set.
        for (unsigned v = 0; v < 3; v++) {
            uint32_t sr = (s_base & ~0xff01u) | (v == 1 ? 0 : ip) | (v == 0 || v == 1 ? 1 : 0);
            resetExc(MODE_IRQ);
            setSR(sr);
            setCause(ip);
            delay(10);
            int before = s_count;
            uint32_t causeMid = getCause();
            if (v == 2) {
                setSR(sr | 1);
                delay(10);
            }
            setSR(s_base);
            uint32_t causeEnd = getCause();
            setCause(0);
            ramsyscall_printf(
                "SWI ip%d case=%d sr=%08x countBeforeIEc=%d count=%d excCause=%08x(exc %d) causeMid=%08x causeEnd=%08x\n",
                8 + sw, v, sr, before, s_count, s_cause, (s_cause >> 2) & 31, causeMid, causeEnd);
        }
    }
}

static int waitIStat(uint32_t bit, unsigned spins) {
    while (spins--) {
        if (ISTAT & bit) return 1;
    }
    return 0;
}

static void hwIrqArms(void) {
    const unsigned SPINS = 2000000;
    uint32_t oldMask = IMASK;
    uint32_t b = s_base & ~0xff01u;
    setSR(b);
    // IP10 against I_STAT & I_MASK, IEc clear.
    // Each MMIO write is read back before mfc0 so the write buffer has drained.
    IMASK = 0;
    ISTAT = 0;
    int got = waitIStat(1, SPINS);
    uint32_t c0 = getCause(), st0 = ISTAT;
    IMASK = 1;
    (void)IMASK;
    uint32_t c1 = getCause();
    IMASK = 0;
    (void)IMASK;
    uint32_t c2 = getCause();
    IMASK = 1;
    (void)IMASK;
    uint32_t c3 = getCause();
    ISTAT = ~1u;
    uint32_t st4 = ISTAT;
    uint32_t c4 = getCause();
    IMASK = 0;
    ramsyscall_printf(
        "IP10 vblank=%d istat=%08x mask0:cause=%08x mask1:cause=%08x mask0:cause=%08x mask1:cause=%08x "
        "acked:cause=%08x istat=%08x\n",
        got, st0, c0, c1, c2, c3, c4, st4);

    // Delivery: IM10 set and clear, IEc set, I_MASK = VBLANK.
    for (unsigned v = 0; v < 2; v++) {
        uint32_t sr = b | 1 | (v ? 0 : 0x400u);
        resetExc(MODE_IRQ);
        ISTAT = 0;
        IMASK = 1;
        setSR(sr);
        got = waitIStat(1, SPINS);
        delay(100);
        uint32_t cmid = getCause();
        setSR(b);
        uint32_t st = ISTAT;
        IMASK = 0;
        ISTAT = 0;
        ramsyscall_printf(
            "IRQ10 im10=%d sr=%08x vblankSeenByPoll=%d count=%d excCause=%08x(exc %d) handlerIStat=%08x "
            "causeMid=%08x istatEnd=%08x\n",
            v ? 0 : 1, sr, got, s_count, s_cause, (s_cause >> 2) & 31, s_irqIStat, cmid, st);
    }

    // Survey: every I_MASK bit on, IEc clear, OR of Cause IP and I_STAT seen.
    IMASK = 0x7ff;
    uint32_t orCause = 0, orStat = 0;
    for (unsigned i = 0; i < SPINS; i++) {
        orCause |= getCause();
        uint32_t st = ISTAT;
        orStat |= st;
        if (st) ISTAT = ~st;
    }
    IMASK = 0;
    ISTAT = 0;
    ramsyscall_printf("SURVEY imask=7ff orCause=%08x orIStat=%08x\n", orCause, orStat);
    IMASK = oldMask;
    setSR(s_base);
}

static void romDump(void) {
    volatile uint32_t *rom = (volatile uint32_t *)0xbfc00100;
    for (unsigned i = 0; i < 0x100 / 4; i += 4) {
        ramsyscall_printf("ROM %08x: %08x %08x %08x %08x\n", (uint32_t)&rom[i], rom[i], rom[i + 1], rom[i + 2],
                          rom[i + 3]);
    }
    char ver[33];
    volatile char *v = (volatile char *)0xbfc7ff32;
    for (unsigned i = 0; i < 32; i++) ver[i] = v[i] >= 32 && v[i] < 127 ? v[i] : '.';
    ver[32] = 0;
    ramsyscall_printf("ROMVER %s\n", ver);
}

int main(void) {
    uint32_t entrySR = getSR();
    uint32_t entryCause = getCause();
    ramsyscall_printf("COP0SR-START sr=%08x cause=%08x imask=%08x\n", entrySR, entryCause, IMASK);
    romDump();
    s_base = entrySR & ~1u;
    setSR(s_base);
    uint32_t oldMask = IMASK;
    IMASK = 0;
    installExceptionHandlers(handler);
    syscall_flushCache();

    sweep();
    cmArm();
    pzArm();
    userArms();
    copArms();
    swIrqArms();
    hwIrqArms();

    // Last: with BEV set, an exception that reaches 0x80000080 prints BEV-RETURNED.
    resetExc(MODE_SKIP);
    bev_probe(s_base & ~1u);
    ramsyscall_printf("BEV0 count=%d cause=%08x(exc %d) epc=%08x\n", s_count, s_cause, (s_cause >> 2) & 31, s_epc);
    ramsyscall_printf("BEV-GO\n");
    delay(200000);
    resetExc(MODE_SKIP);
    bev_probe((s_base & ~1u) | 0x00400000u);
    ramsyscall_printf("BEV-RETURNED count=%d cause=%08x\n", s_count, s_cause);

    uninstallExceptionHandlers();
    syscall_flushCache();
    IMASK = oldMask;
    setSR(entrySR);
    ramsyscall_printf("COP0SR-DONE\n");
    return 0;
}
