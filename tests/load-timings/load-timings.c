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

/* Load timing tests.
 *
 * The R3000A has no data cache (that SRAM is repurposed as the scratchpad), so
 * every data load is a real access whose cost depends on what it hits. These
 * tests characterize that on real silicon:
 *
 *   loadCostByTarget - back-to-back `lw` cost per target. Scratchpad is
 *       single-cycle (on-chip SRAM, no bus); on-die MMIO is ~5 cyc; main RAM is
 *       ~7 cyc and cached (KSEG0) equals uncached (KSEG1), reconfirming there is
 *       no data cache; BIOS ROM is tens of cycles (8-bit ROM behind a slow,
 *       programmable bus delay: 27 with COM_DELAY at 00031125h, 33 at
 *       0000132Ch, which the BIOS CD-ROM read routine leaves behind; the suite
 *       pins it, see below).
 *
 *   busDelaySweep - which DEV2/DEV4 delay/size bit and which COM_DELAY nibble
 *       moves the cost of which access.
 *
 *   onDieUniformity - every on-die MMIO register (interrupt controller, DMA,
 *       root counters) reads at the same cost: one decoder, one latency.
 *
 *   loadShadow - the interesting one. An uncached load does NOT fully stall the
 *       pipeline: its bus access overlaps following independent instructions.
 *       But it does NOT fully hide either. Sweeping 32 loads each followed by s
 *       trailing nops, the cost-over-nops drops as s grows and then FLATLINES at
 *       ~2 cyc/load from s>=4: about half the 4-cyc bus stall overlaps trailing
 *       work, and ~2 cyc/load of bus occupancy is irreducible no matter how many
 *       independent instructions follow.
 *
 *   loadShadowByTarget / loadInterlockedByTarget (by stenzek) - the two sweeps
 *       above only ever looked at I_STAT. These repeat them against every kind
 *       of target, out to 48 instructions, to find out whether the shadow is a
 *       property of on-die registers or of loads in general, and for each target
 *       how much of the access holds the CPU up, how much can be hidden, and for
 *       how long.
 *
 * Delay registers: the ROM, CD-ROM and SPU costs follow the memory-control
 * registers, and those depend on how the console booted - a disc boot leaves
 * COM_DELAY at 0000132Ch and CDROM_DELAY at 00020943h, a boot without one
 * 00031125h and 00020843h. The suite saves all five delay registers on entry,
 * pins COM_DELAY and CDROM_DELAY to the latter pair, and restores the five on
 * exit, so every figure below is independent of how the console got here.
 *
 * Cycle source: root counter 2 in system-clock mode (1 tick / CPU cycle,
 * 16-bit), via the COUNTERS macro. IRQs masked suite-wide; minimum taken over
 * several runs to reject stray stalls and ensure warmed icache.
 *
 * These are hardware-timing tests: the emulator does not model these access
 * costs, so every check is a CESTER_MAYBE_TEST and is skipped under PCSX_TESTS.
 */

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/hardware/counters.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

/* R3000A cache/bus-interface control. The suite does not write it; the test
   below pins what it reads, so every cost in this file names its regime. */
#define BIU_CONFIG_ADDR 0xfffe0130u
#define BIU_EXPECTED    0x0001e988u

// clang-format off

/* Back-to-back block size and spacing-sweep load count. 256 reads = 1 KiB of
   unrolled code, inside the 4 KiB icache; 256 * ~33 cyc (slowest target) stays
   under the counter's 16-bit wrap. */
#define N_READS 256
#define N_LOADS 32

#define REP4(x)   x x x x
#define REP16(x)  REP4(x)  REP4(x)  REP4(x)  REP4(x)
#define REP32(x)  REP16(x) REP16(x)
#define REP64(x)  REP16(x) REP16(x) REP16(x) REP16(x)
#define REP128(x) REP64(x) REP64(x)
#define REP256(x) REP64(x) REP64(x) REP64(x) REP64(x)

/* Load targets, spanning the access-cost hierarchy. */
#define ADDR_SCRATCH 0x1f800000u  /* scratchpad SRAM (fast on-chip, no bus)     */
#define ADDR_ISTAT   0xbf801070u  /* on-die MMIO: interrupt controller I_STAT   */
#define ADDR_DMA     0xbf8010a0u  /* on-die MMIO: DMA channel 2 MADR            */
#define ADDR_RAM_C   0x80100000u  /* main RAM, cached mirror (KSEG0)            */
#define ADDR_RAM_U   0xa0100000u  /* main RAM, uncached mirror (KSEG1)          */
#define ADDR_BIOS    0xbfc00000u  /* BIOS ROM (KSEG1)                           */
#define ADDR_JOYSTAT 0xbf801044u  /* on-die MMIO: controller port JOY_STAT      */
#define ADDR_GPUSTAT 0xbf801814u  /* GPU status - the GPU is a separate chip    */
#define ADDR_MDECST  0xbf801824u  /* MDEC status                                */
#define ADDR_CDROM   0xbf801800u  /* CD-ROM controller index/status (8-bit bus) */
#define ADDR_SPU     0xbf801d80u  /* SPU main volume left (16-bit bus)          */

/* One measured block = N_LOADS iterations of (lw + s nops), bracketed by two
   counter-2 reads. Defined as a macro so each spacing gets its own unrolled
   body; invoked inside CESTER_BODY below. */
#define MAKE_SPACED(name, seq)                                             \
    static __attribute__((always_inline)) uint32_t name(volatile void *p) {                              \
        register uint32_t sink;                                           \
        uint16_t before, after;                                           \
        before = COUNTERS[2].value;                                       \
        __asm__ volatile(REP32(seq) : "=&r"(sink) : "r"(p) : "memory");   \
        after = COUNTERS[2].value;                                        \
        (void)sink;                                                       \
        return (uint16_t)(after - before);                               \
    }

/* Two loads from one address separated by a gap that touches no bus at all: a
   counted loop running out of the icache. If the DRAM controller holds a row
   open and times it out, a load should cost more once the gap exceeds the
   timeout. The nop arm carries an identical delay, so the difference between
   the arms isolates the load and the nop arm's own slope calibrates
   cycles-per-delay-unit rather than my assuming it. */
#define MAKE_GAP(name, op)                                                              \
    static __attribute__((always_inline)) uint32_t name(volatile void *p, uint32_t d) { \
        register uint32_t sink;                                                         \
        uint16_t before, after;                                                         \
        before = COUNTERS[2].value;                                                     \
        __asm__ volatile(REP32(op "\n"                                                  \
                               "nop\n"                                                  \
                               "addiu %0, %0, 1\n"                                      \
                               "move $t9, %2\n"                                         \
                               "1: addiu $t9, $t9, -1\n"                                \
                               "bgtz $t9, 1b\n"                                         \
                               "nop\n")                                                 \
                         : "=&r"(sink) : "r"(p), "r"(d) : "$t9", "memory");             \
        after = COUNTERS[2].value;                                                       \
        (void)sink;                                                                      \
        return (uint16_t)(after - before);                                              \
    }

/* Store-then-load probes. The write queue is 4 words deep and is reported to act
   as a pass-through read cache; if it does, a load of an address still sitting
   in the queue is answered by the queue rather than by DRAM, and is cheaper than
   a load of an address that was never written. Both arms are interlocked so the
   load's full latency is on the critical path - without that they measure bus
   occupancy and are blind, which is how the first version of the gap probe
   wasted a ticket. */
#define MAKE_RAW(name, seq)                                                                   \
    static __attribute__((always_inline)) uint32_t name(volatile void *p, volatile void *q) { \
        register uint32_t sink;                                                               \
        uint16_t before, after;                                                               \
        before = COUNTERS[2].value;                                                           \
        __asm__ volatile(REP32(seq) : "=&r"(sink) : "r"(p), "r"(q) : "memory");                \
        after = COUNTERS[2].value;                                                             \
        (void)sink;                                                                            \
        return (uint16_t)(after - before);                                                    \
    }

/* 16 stores at fixed immediate offsets: one instruction per store, so the issue
   rate is one per cycle and the 4-deep queue actually saturates. The previous
   version bumped a pointer between stores, issuing at exactly the drain rate. */
#define SW16                                                                \
    "sw $0, 0(%0)\n"  "sw $0, 4(%0)\n"  "sw $0, 8(%0)\n"  "sw $0, 12(%0)\n" \
    "sw $0, 16(%0)\n" "sw $0, 20(%0)\n" "sw $0, 24(%0)\n" "sw $0, 28(%0)\n" \
    "sw $0, 32(%0)\n" "sw $0, 36(%0)\n" "sw $0, 40(%0)\n" "sw $0, 44(%0)\n" \
    "sw $0, 48(%0)\n" "sw $0, 52(%0)\n" "sw $0, 56(%0)\n" "sw $0, 60(%0)\n"

/* A KSEG1 store is reported not to bypass the write queue but to SYNCHRONIZE it:
   the store enters the queue, then stalls until the queue is fully drained. A
   stream of KSEG1 stores therefore shows nothing, because each one leaves the
   queue empty for the next - which is exactly what the mirror comparison
   measured. The discriminating sequence is mixed: prime the queue with N KSEG0
   stores, then issue one KSEG1 store and time it. Under that model the cost
   rises with N and knees once N reaches the queue depth, so the knee measures
   the depth from the outside. */
#define MAKE_PRIME(name, primers, tail)                                                       \
    static __attribute__((always_inline)) uint32_t name(volatile void *c, volatile void *u) { \
        uint16_t before, after;                                                               \
        before = COUNTERS[2].value;                                                           \
        __asm__ volatile(REP16(primers tail) : : "r"(c), "r"(u) : "memory");                  \
        after = COUNTERS[2].value;                                                            \
        return (uint16_t)(after - before);                                                    \
    }

/* Bus delay/size sweep. The memory-control registers set the access timing of
   each external device; COM_DELAY supplies the per-cycle values that bits 8-11
   of a device register switch in (bit 8 recovery = COM0, bit 9 hold = COM1,
   bit 10 float = COM2, bit 11 pre-strobe = COM3). Each arm rewrites one
   device register and/or COM_DELAY, times N_BUS back-to-back accesses, and
   restores both before printing. */
#define DEV2_DELAY (*(volatile uint32_t *)0xbf801010)
#define DEV4_DELAY (*(volatile uint32_t *)0xbf801014)
#define COM_DELAY  (*(volatile uint32_t *)0xbf801020)
#define CDROM_DELAY (*(volatile uint32_t *)0xbf801018)
/* The five delay registers, 1F801010h-1F801020h: BIOS ROM (DEV2), SPU (DEV4),
   CD-ROM (DEV5), Expansion 2, COM_DELAY. */
#define DELAY_REGS ((volatile uint32_t *)0xbf801010)
#define PIN_COM_DELAY   0x00031125u
#define PIN_CDROM_DELAY 0x00020843u
#define ADDR_SPU_ADSR 0xbf801c08u /* voice 0 ADSR: unkeyed, harmless to write */
#define N_BUS 64

#define MAKE_BUS(name, insn)                                                    \
    static __attribute__((always_inline)) uint32_t name(volatile void *p) {    \
        register uint32_t sink = 0;                                             \
        uint16_t before, after;                                                 \
        before = COUNTERS[2].value;                                             \
        __asm__ volatile(REP64(insn "\n") : "+r"(sink) : "r"(p) : "memory");    \
        after = COUNTERS[2].value;                                              \
        (void)sink;                                                             \
        return (uint16_t)(after - before);                                      \
    }

#define RUN8(lo, hi, expr) do {                     \
        lo = 0xffffu; hi = 0u;                      \
        for (int i_ = 0; i_ < 8; i_++) {            \
            uint32_t d_ = (expr);                   \
            if (d_ < lo) lo = d_;                   \
            if (d_ > hi) hi = d_;                   \
        }                                           \
    } while (0)

#define PRIME0 ""
#define PRIME1 PRIME0 "sw $0, 0(%0)\n"
#define PRIME2 PRIME1 "sw $0, 4(%0)\n"
#define PRIME3 PRIME2 "sw $0, 8(%0)\n"
#define PRIME4 PRIME3 "sw $0, 12(%0)\n"
#define PRIME5 PRIME4 "sw $0, 16(%0)\n"
#define PRIME6 PRIME5 "sw $0, 20(%0)\n"

/* By-target sweeps. Same idea as MAKE_SPACED, with three differences:
    - the repetition is done by the assembler, so the spacing can go out far
      enough to find the end of the shadow on the slow targets without a macro
      per length;
    - fewer repetitions, so the longest block still fits in the icache;
    - 8 nops either side, so neither counter read is inside the shadow of a
      load being measured, or has one of them inside its own.
   noreorder, because the assembler otherwise puts a nop of its own between a
   load and an instruction which uses its result. */
#define SWEEP_PAD ".rept 8\nnop\n.endr\n"
#define SHADOW_REPS 16
#define INTLCK_REPS 8
#define INTLCK_LEN  48
#define SWEEP_STR_(x) #x
#define SWEEP_STR(x)  SWEEP_STR_(x)

/* SHADOW_REPS x (op + s nops). With op being a load, the next load starts s + 1
   instructions later, and nothing uses the result. */
#define MAKE_SHADOW(name, op, s)                                               \
    static __attribute__((always_inline)) uint32_t name(volatile void *p) {    \
        register uint32_t sink;                                                \
        uint16_t before, after;                                                \
        before = COUNTERS[2].value;                                            \
        __asm__ volatile(".set push\n.set noreorder\n" SWEEP_PAD               \
                         ".rept " SWEEP_STR(SHADOW_REPS) "\n" op "\n"         \
                         ".rept " #s "\nnop\n.endr\n.endr\n"               \
                         SWEEP_PAD ".set pop\n"                                \
                         : "=&r"(sink) : "r"(p) : "memory");                   \
        after = COUNTERS[2].value;                                             \
        (void)sink;                                                            \
        return (uint16_t)(after - before);                                     \
    }

/* INTLCK_REPS x (load, then INTLCK_LEN instructions, the k'th of which is
   `use`, and the rest nops). With use being the addiu, the result is read k
   instructions after the load. k = 1 is the load delay slot, which sees the
   old value of the register. The next load is always INTLCK_LEN + 1
   instructions away. */
#define MAKE_INTLCK(name, op, use, k)                                          \
    static __attribute__((always_inline)) uint32_t name(volatile void *p) {    \
        register uint32_t sink;                                                \
        uint16_t before, after;                                                \
        before = COUNTERS[2].value;                                            \
        __asm__ volatile(".set push\n.set noreorder\n" SWEEP_PAD               \
                         ".rept " SWEEP_STR(INTLCK_REPS) "\n" op "\n"         \
                         ".rept " #k " - 1\nnop\n.endr\n"                      \
                         use "\n"                                              \
                         ".rept " SWEEP_STR(INTLCK_LEN) " - " #k "\nnop\n.endr\n" \
                         ".endr\n" SWEEP_PAD ".set pop\n"                      \
                         : "=&r"(sink) : "r"(p) : "memory");                   \
        after = COUNTERS[2].value;                                             \
        (void)sink;                                                            \
        return (uint16_t)(after - before);                                     \
    }

/* The points sampled. Every instruction out to 12, which covers the on-die
   shadow several times over, then coarser steps out to 48 for the targets
   whose access takes tens of cycles. */
#define SHADOW_POINTS(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) \
    X(14) X(16) X(20) X(24) X(32) X(40) X(48)
#define INTLCK_POINTS(X) \
    X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) \
    X(14) X(16) X(20) X(24) X(32) X(40) X(48)

#define SWEEP_LIST(n) n,
#define SHADOW_COUNT  (sizeof(s_shadowPoints) / sizeof(s_shadowPoints[0]))
#define INTLCK_COUNT  (sizeof(s_intlckPoints) / sizeof(s_intlckPoints[0]))
/* Where s=8 is in SHADOW_POINTS, which is what the instrument check looks at. */
#define SHADOW_CHECK  8

/* Word loads for the 32-bit targets, byte loads for the 8 and 16-bit devices,
   where a word load is several accesses, and on the CD-ROM controller would
   read registers that reading has an effect on. */
#define SHADOW_FNS(s)                                \
    MAKE_SHADOW(shadow_w_##s, "lw %0, 0(%1)", s)     \
    MAKE_SHADOW(shadow_b_##s, "lbu %0, 0(%1)", s)    \
    MAKE_SHADOW(shadow_n_##s, "nop", s)
#define INTLCK_FNS(k)                                                      \
    MAKE_INTLCK(intlck_w_##k, "lw %0, 0(%1)", "addiu %0, %0, 1", k)        \
    MAKE_INTLCK(intlck_b_##k, "lbu %0, 0(%1)", "addiu %0, %0, 1", k)

#define SHADOW_RUN_W(s) { BENCH(x, shadow_w_##s, M); BENCH(y, shadow_n_##s, M); a[i] = x; b[i] = y; i++; }
#define SHADOW_RUN_B(s) { BENCH(x, shadow_b_##s, M); BENCH(y, shadow_n_##s, M); a[i] = x; b[i] = y; i++; }
#define INTLCK_RUN_W(k) { BENCH(x, intlck_w_##k, M); a[i] = x; b[i] = base; i++; }
#define INTLCK_RUN_B(k) { BENCH(x, intlck_b_##k, M); a[i] = x; b[i] = base; i++; }

CESTER_BODY(
    static int s_interruptsWereEnabled;

    /* The five delay registers as found at suite entry, restored at exit. */
    static uint32_t s_entryDelay[5];

    static void pinDelayRegs(void) {
        COM_DELAY = PIN_COM_DELAY;
        CDROM_DELAY = PIN_CDROM_DELAY;
    }

    /* N_READS back-to-back `lw` from an address, bracketed by counter-2 reads. */
    static __attribute__((always_inline)) uint32_t timed_read(volatile void *p) {
        register uint32_t sink;
        uint16_t before, after;
        before = COUNTERS[2].value;
        __asm__ volatile(REP256("lw %0, 0(%1)\n") : "=&r"(sink) : "r"(p) : "memory");
        after = COUNTERS[2].value;
        (void)sink;
        return (uint16_t)(after - before);
    }

    /* Structurally identical baseline: same bracket, N_READS nops. */
    static __attribute__((always_inline)) uint32_t timed_nop(volatile void *p) {
        register uint32_t sink;
        uint16_t before, after;
        before = COUNTERS[2].value;
        __asm__ volatile(REP256("nop\n") : "=&r"(sink) : "r"(p) : "memory");
        after = COUNTERS[2].value;
        (void)sink;
        return (uint16_t)(after - before);
    }

    /* 128 pairs of `lw`, alternating between two addresses = 256 loads, so the
       result is directly comparable to the N_READS blocks above. With both
       pointers equal this degenerates to timed_read and must reproduce its
       figure - that equality is the instrument check for the stride sweep. */
    static __attribute__((always_inline)) uint32_t timed_pair(volatile void *a, volatile void *b) {
        register uint32_t sink;
        uint16_t before, after;
        before = COUNTERS[2].value;
        __asm__ volatile(REP128("lw %0, 0(%1)\nlw %0, 0(%2)\n")
                         : "=&r"(sink) : "r"(a), "r"(b) : "memory");
        after = COUNTERS[2].value;
        (void)sink;
        return (uint16_t)(after - before);
    }

    /* Same block shape as timed_read, but storing. Writes are buffered by the
       write queue through KSEG0; KSEG1 is documented to bypass it, so the two
       mirrors should differ for stores even though they are identical for
       loads. */
    static __attribute__((always_inline)) uint32_t timed_write(volatile void *p) {
        uint16_t before, after;
        register volatile char *q = (volatile char *)p;
        before = COUNTERS[2].value;
        __asm__ volatile(REP256("sw $0, 0(%0)\naddiu %0, %0, 4\n") : "+r"(q) : : "memory");
        after = COUNTERS[2].value;
        return (uint16_t)(after - before);
    }

    /* Same pointer walk, no store: isolates the store from the address bump. */
    static __attribute__((always_inline)) uint32_t timed_walk(volatile void *p) {
        uint16_t before, after;
        register volatile char *q = (volatile char *)p;
        before = COUNTERS[2].value;
        __asm__ volatile(REP256("nop\naddiu %0, %0, 4\n") : "+r"(q) : : "memory");
        after = COUNTERS[2].value;
        return (uint16_t)(after - before);
    }

    /* Saturating store burst: one instruction per store, 16 distinct addresses
       cycling so no two adjacent stores hit the same word. */
    static __attribute__((always_inline)) uint32_t timed_burst(volatile void *p) {
        uint16_t before, after;
        before = COUNTERS[2].value;
        __asm__ volatile(REP16(SW16) : : "r"(p) : "memory");
        after = COUNTERS[2].value;
        return (uint16_t)(after - before);
    }

    MAKE_GAP(gap_load, "lw %0, 0(%1)")
    MAKE_GAP(gap_nop, "nop")

    MAKE_PRIME(prime0, PRIME0, "sw $0, 0(%1)\n")
    MAKE_PRIME(prime1, PRIME1, "sw $0, 0(%1)\n")
    MAKE_PRIME(prime2, PRIME2, "sw $0, 0(%1)\n")
    MAKE_PRIME(prime3, PRIME3, "sw $0, 0(%1)\n")
    MAKE_PRIME(prime4, PRIME4, "sw $0, 0(%1)\n")
    MAKE_PRIME(prime5, PRIME5, "sw $0, 0(%1)\n")
    MAKE_PRIME(prime6, PRIME6, "sw $0, 0(%1)\n")
    MAKE_PRIME(pbase0, PRIME0, "nop\n")
    MAKE_PRIME(pbase1, PRIME1, "nop\n")
    MAKE_PRIME(pbase2, PRIME2, "nop\n")
    MAKE_PRIME(pbase3, PRIME3, "nop\n")
    MAKE_PRIME(pbase4, PRIME4, "nop\n")
    MAKE_PRIME(pbase5, PRIME5, "nop\n")
    MAKE_PRIME(pbase6, PRIME6, "nop\n")

    /* Plain function, not a macro: a #define inside a cester test body is a
       preprocessor directive inside a macro argument, which does not expand. */
    typedef uint32_t (*PrimeFn)(volatile void *, volatile void *);
    static void primeRow(int n, PrimeFn sync, PrimeFn base, volatile void *c, volatile void *u) {
        uint32_t bs = 0xffffu, bb = 0xffffu;
        for (int i = 0; i < 8; i++) {
            uint32_t x = sync(c, u);
            uint32_t y = base(c, u);
            if (x < bs) bs = x;
            if (y < bb) bb = y;
        }
        ramsyscall_printf("  PRIME n=%d sync=%u base=%u delta=%d\n", n, bs, bb, (int)bs - (int)bb);
    }

    MAKE_RAW(raw_same, "sw $0, 0(%1)\nlw %0, 0(%1)\nnop\naddiu %0, %0, 1\n")
    MAKE_RAW(raw_other, "sw $0, 0(%1)\nlw %0, 0(%2)\nnop\naddiu %0, %0, 1\n")
    MAKE_RAW(raw_noload, "sw $0, 0(%1)\nnop\nnop\naddiu %0, %0, 1\n")

    MAKE_SPACED(spaced0, "lw %0, 0(%1)\n")
    MAKE_SPACED(spaced1, "lw %0, 0(%1)\nnop\n")
    MAKE_SPACED(spaced2, "lw %0, 0(%1)\nnop\nnop\n")
    MAKE_SPACED(spaced3, "lw %0, 0(%1)\nnop\nnop\nnop\n")
    MAKE_SPACED(spaced4, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\n")
    MAKE_SPACED(spaced5, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\nnop\n")
    MAKE_SPACED(spaced6, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\nnop\nnop\n")
    MAKE_SPACED(spaced7, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")

    /* The load delay means the s0 version can't happen. */
    MAKE_SPACED(intlck1, "lw %0, 0(%1)\nnop\naddiu %0, 1\nnop\nnop\nnop\nnop\nnop\nnop\n")
    MAKE_SPACED(intlck2, "lw %0, 0(%1)\nnop\nnop\naddiu %0, 1\nnop\nnop\nnop\nnop\nnop\n")
    MAKE_SPACED(intlck3, "lw %0, 0(%1)\nnop\nnop\nnop\naddiu %0, 1\nnop\nnop\nnop\nnop\n")
    MAKE_SPACED(intlck4, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\naddiu %0, 1\nnop\nnop\nnop\n")
    MAKE_SPACED(intlck5, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\nnop\naddiu %0, 1\nnop\nnop\n")
    MAKE_SPACED(intlck6, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\nnop\nnop\naddiu %0, 1\nnop\n")
    MAKE_SPACED(intlck7, "lw %0, 0(%1)\nnop\nnop\nnop\nnop\nnop\nnop\nnop\naddiu %0, 1\n")

    MAKE_BUS(bus_nop, "nop")
    MAKE_BUS(bus_lw, "lw %0, 0(%1)")
    MAKE_BUS(bus_lh, "lh %0, 0(%1)")
    MAKE_BUS(bus_lb, "lb %0, 0(%1)")
    MAKE_BUS(bus_sh, "sh %0, 0(%1)")
    MAKE_BUS(bus_sw, "sw %0, 0(%1)")
    MAKE_BUS(bus_lhs, "lh %0, 0(%1)")
    MAKE_BUS(bus_lws, "lw %0, 0(%1)")
    MAKE_BUS(bus_lbu, "lbu %0, 0(%1)")

    typedef struct {
        const char *name;
        uint32_t clr, set, flip;
        int nib;
        uint32_t val;
    } BusArm;
    static const BusArm s_busArms[] = {
        {"base", 0, 0, 0, -1, 0},
        {"b8^", 0, 0, 0x100, -1, 0},
        {"b9^", 0, 0, 0x200, -1, 0},
        {"b10^", 0, 0, 0x400, -1, 0},
        {"b11^", 0, 0, 0x800, -1, 0},
        {"b8-11", 0, 0xf00, 0, -1, 0},
        {"rd0", 0xf0, 0, 0, -1, 0},
        {"rd15", 0, 0xf0, 0, -1, 0},
        {"wr0", 0xf, 0, 0, -1, 0},
        {"wr15", 0, 0xf, 0, -1, 0},
        {"com0=0", 0, 0x100, 0, 0, 0},
        {"com0=5", 0, 0x100, 0, 0, 5},
        {"com0=15", 0, 0x100, 0, 0, 15},
        {"com1=0", 0, 0x200, 0, 1, 0},
        {"com1=5", 0, 0x200, 0, 1, 5},
        {"com1=15", 0, 0x200, 0, 1, 15},
        {"com2=0", 0, 0x400, 0, 2, 0},
        {"com2=5", 0, 0x400, 0, 2, 5},
        {"com2=15", 0, 0x400, 0, 2, 15},
        {"com3=0", 0, 0x800, 0, 3, 0},
        {"com3=5", 0, 0x800, 0, 3, 5},
        {"com3=15", 0, 0x800, 0, 3, 15},
        {"base", 0, 0, 0, -1, 0},
    };

    /* CDROM_DELAY bit 8 against COM_DELAY, at the values a boot with and
       without a disc leaves, with the min ticks of N_BUS byte loads from the
       CD-ROM controller (1F801800h) and the SPU (1F801D80h) each pair gives. */
    typedef struct {
        uint32_t cd, com;
        uint32_t cdrom, spu;
    } CdArm;
    static const CdArm s_cdArms[] = {
        {0x00020843u, 0x00031125u, 646, 1349},
        {0x00020843u, 0x0000132cu, 646, 1790},
        {0x00020943u, 0x00031125u, 709, 1349},
        {0x00020943u, 0x0000132cu, 1150, 1790},
    };

    /* Take the min over 8 runs, which should ensure warm icache and no stray
       stalls. Back to back, all eight runs start at the same phase, and on
       RAM targets some phases add one cycle that the min then keeps; a spin
       of 19 * i iterations before run i moves each run to its own phase. It
       stays inline: a call here costs the timed code its warm icache. */
#define BENCH(ret, fn, p) uint32_t ret; do { \
        uint32_t best = 0xffffu;             \
        for (int i = 0; i < 8; i++) {        \
            uint32_t skew = i * 19u;         \
            __asm__ volatile(".set push\n.set noreorder\n1: bnez %0, 1b\naddiu %0, %0, -1\n.set pop\n" \
                             : "+r"(skew));  \
            uint32_t d = fn(p);              \
            if (d < best) best = d;          \
        }                                    \
        ret = best;                          \
    } while (0);

    static void report(const char *name, uint32_t raw, uint32_t base) {
        uint32_t abs_cc = (raw * 100u + N_READS / 2) / N_READS;
        uint32_t marg_cc = ((raw - base) * 100u + N_READS / 2) / N_READS;
        ramsyscall_printf("  %s raw256=%u  abs=%u.%02u  marginal=%u.%02u cyc/read\n", name, raw,
                          abs_cc / 100u, abs_cc % 100u, marg_cc / 100u, marg_cc % 100u);
    }

    SHADOW_POINTS(SHADOW_FNS)
    INTLCK_POINTS(INTLCK_FNS)

    /* What the interlocked arms are compared against: the same load and the
       same 48 instructions after it, none of which use the result. */
    MAKE_INTLCK(intlck_w_base, "lw %0, 0(%1)", "nop", 1)
    MAKE_INTLCK(intlck_b_base, "lbu %0, 0(%1)", "nop", 1)

    static const uint8_t s_shadowPoints[] = { SHADOW_POINTS(SWEEP_LIST) };
    static const uint8_t s_intlckPoints[] = { INTLCK_POINTS(SWEEP_LIST) };

    /* Three lines per target: the ticks for each point, what they are compared
       against, and the difference per load in hundredths of a cycle. */
    static void sweepPrint(const char *sweep, const char *op, const char *target, const uint8_t *points,
                           unsigned count, const uint32_t *a, const uint32_t *b, unsigned reps) {
        ramsyscall_printf("  %s %s %s ticks:", sweep, op, target);
        for (unsigned i = 0; i < count; i++) ramsyscall_printf(" %u=%u", points[i], a[i]);
        ramsyscall_printf("\n  %s %s %s base :", sweep, op, target);
        for (unsigned i = 0; i < count; i++) ramsyscall_printf(" %u=%u", points[i], b[i]);
        ramsyscall_printf("\n  %s %s %s cyc  :", sweep, op, target);
        for (unsigned i = 0; i < count; i++) {
            int d = (int)a[i] - (int)b[i];
            unsigned m = (unsigned)(d < 0 ? -d : d);
            unsigned cc = (m * 100u + reps / 2u) / reps;
            ramsyscall_printf(" %u=%s%u.%02u", points[i], d < 0 ? "-" : "", cc / 100u, cc % 100u);
        }
        ramsyscall_printf("\n");
    }

    /* Not inlined: each of these has every block of its sweep inside it. The
       ticks and what they are compared against go to a and b for the asserts. */
    static __attribute__((noinline)) uint32_t shadowSweepW(const char *target, volatile void *M, uint32_t *a, uint32_t *b) {
        unsigned i = 0;
        SHADOW_POINTS(SHADOW_RUN_W)
        sweepPrint("SHADOW", "lw ", target, s_shadowPoints, SHADOW_COUNT, a, b, SHADOW_REPS);
        return a[SHADOW_CHECK] - b[SHADOW_CHECK];
    }

    static __attribute__((noinline)) uint32_t shadowSweepB(const char *target, volatile void *M, uint32_t *a, uint32_t *b) {
        unsigned i = 0;
        SHADOW_POINTS(SHADOW_RUN_B)
        sweepPrint("SHADOW", "lbu", target, s_shadowPoints, SHADOW_COUNT, a, b, SHADOW_REPS);
        return a[SHADOW_CHECK] - b[SHADOW_CHECK];
    }

    static __attribute__((noinline)) void intlckSweepW(const char *target, volatile void *M, uint32_t *a, uint32_t *b) {
        unsigned i = 0;
        BENCH(base, intlck_w_base, M);
        INTLCK_POINTS(INTLCK_RUN_W)
        sweepPrint("INTLCK", "lw ", target, s_intlckPoints, INTLCK_COUNT, a, b, INTLCK_REPS);
    }

    static __attribute__((noinline)) void intlckSweepB(const char *target, volatile void *M, uint32_t *a, uint32_t *b) {
        unsigned i = 0;
        BENCH(base, intlck_b_base, M);
        INTLCK_POINTS(INTLCK_RUN_B)
        sweepPrint("INTLCK", "lbu", target, s_intlckPoints, INTLCK_COUNT, a, b, INTLCK_REPS);
    }

    /* Expected ticks for the by-target sweeps, min over 8 runs. SCPH-1000,
       1001, 5501 and 7001, which boot with the delay register values the suite
       pins, gave these byte for byte. Rows are in SHADOW_POINTS / INTLCK_POINTS
       order. The MMIO rows serve I_STAT, JOY_STAT, GPUSTAT and MDEC status
       alike, on-die or not. */
    static const uint16_t s_shNop[] = {35, 51, 67, 83, 99, 115, 131, 147, 163, 179, 195, 211, 227, 259, 291, 355, 419, 547, 675, 803};
    static const uint16_t s_shMmio[] = {97, 113, 129, 130, 131, 147, 163, 179, 195, 211, 227, 243, 259, 291, 323, 387, 451, 579, 707, 835};
    static const uint16_t s_shRam[] = {127, 143, 159, 160, 161, 162, 163, 179, 195, 211, 227, 243, 259, 291, 323, 387, 451, 579, 707, 835};
    static const uint16_t s_shRomW[] = {445, 460, 475, 475, 475, 475, 475, 475, 475, 475, 475, 475, 475, 475, 475, 477, 481, 579, 707, 835};
    static const uint16_t s_shRomB[] = {157, 173, 189, 190, 191, 192, 193, 194, 195, 211, 227, 243, 259, 291, 323, 387, 451, 579, 707, 835};
    static const uint16_t s_shCdrom[] = {173, 188, 204, 205, 206, 207, 208, 209, 210, 211, 227, 243, 259, 291, 323, 387, 451, 579, 707, 835};
    static const uint16_t s_shSpu[] = {348, 348, 363, 363, 363, 363, 363, 363, 363, 363, 363, 363, 364, 366, 368, 387, 451, 579, 707, 835};
    static const uint16_t s_ilMmio[] = {443, 443, 443, 435, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427};
    static const uint16_t s_ilScratch[] = {411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411, 411};
    static const uint16_t s_ilRam[] = {459, 459, 459, 451, 443, 435, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427};
    static const uint16_t s_ilRomW[] = {619, 619, 619, 611, 603, 595, 587, 579, 571, 563, 555, 547, 531, 515, 483, 451, 427, 427, 427};
    static const uint16_t s_ilRomB[] = {475, 475, 475, 467, 459, 451, 443, 435, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427};
    static const uint16_t s_ilCdrom[] = {483, 483, 483, 475, 467, 459, 451, 443, 435, 427, 427, 427, 427, 427, 427, 427, 427, 427, 427};
    static const uint16_t s_ilSpu[] = {563, 563, 563, 555, 547, 539, 531, 523, 515, 507, 499, 491, 475, 459, 427, 427, 427, 427, 427};

    /* The targets both sweeps visit, in the order they visit them. A scratchpad
       load is a nop as far as the shadow sweep can tell, so its load row is the
       nop row. */
    typedef struct {
        const char *name;
        uint32_t addr;
        int byte;
        const uint16_t *shadow, *intlck;
        uint32_t intlckBase;
    } SweepTarget;
    static const SweepTarget s_sweepTargets[] = {
        {"I_STAT    ", ADDR_ISTAT,   0, s_shMmio, s_ilMmio,   427},
        {"JOY_STAT  ", ADDR_JOYSTAT, 0, s_shMmio, s_ilMmio,   427},
        {"GPUSTAT   ", ADDR_GPUSTAT, 0, s_shMmio, s_ilMmio,   427},
        {"MDEC stat ", ADDR_MDECST,  0, s_shMmio, s_ilMmio,   427},
        {"scratchpad", ADDR_SCRATCH, 0, s_shNop,   s_ilScratch, 411},
        {"RAM cached", ADDR_RAM_C,   0, s_shRam,   s_ilRam,     427},
        {"RAM uncach", ADDR_RAM_U,   0, s_shRam,   s_ilRam,     427},
        {"BIOS ROM  ", ADDR_BIOS,    0, s_shRomW,  s_ilRomW,    427},
        {"I_STAT    ", ADDR_ISTAT,   1, s_shMmio, s_ilMmio,   427},
        {"RAM uncach", ADDR_RAM_U,   1, s_shRam,   s_ilRam,     427},
        {"BIOS ROM  ", ADDR_BIOS,    1, s_shRomB,  s_ilRomB,    427},
        {"CD-ROM    ", ADDR_CDROM,   1, s_shCdrom, s_ilCdrom,   427},
        {"SPU       ", ADDR_SPU,     1, s_shSpu,   s_ilSpu,     427},
    };
#define SWEEP_TARGETS (sizeof(s_sweepTargets) / sizeof(s_sweepTargets[0]))

    /* Number of points in a row that differ from what is expected, each one
       printed, so a failing assert names its target and point. */
    static unsigned sweepMismatches(const char *sweep, const char *what, const SweepTarget *t,
                                    const uint8_t *points, unsigned count, const uint32_t *got,
                                    const uint16_t *row, uint32_t flat) {
        unsigned bad = 0;
        for (unsigned i = 0; i < count; i++) {
            uint32_t want = row ? row[i] : flat;
            if (got[i] == want) continue;
            ramsyscall_printf("  MISMATCH %s %s %s %s %u: got %u, expected %u\n", sweep, t->byte ? "lbu" : "lw ",
                              t->name, what, points[i], got[i], want);
            bad++;
        }
        return bad;
    }
)

CESTER_BEFORE_ALL(load_tests,
#ifdef LOAD_TIMINGS_ATCONS
    /* Dev boards with an ATCONS console: route stdout there (C0(1Bh), installStdIo). */
    {
        register int n asm("t1") = 0x1b;
        __asm__ volatile("" : "=r"(n) : "r"(n));
        ((void (*)(int))0xc0)(1);
    }
#endif
    /* Mask interrupts across the timed regions; set root counter 2 to the
       system-clock source (bits 8-9 = 00), free running. Writing mode resets
       the counter value to 0. */
    s_interruptsWereEnabled = enterCriticalSection();
    COUNTERS[2].mode = 0;
    /* Pin the delay registers to the values a boot without a disc leaves. */
    for (int i = 0; i < 5; i++) s_entryDelay[i] = DELAY_REGS[i];
    pinDelayRegs();
)

CESTER_AFTER_ALL(load_tests,
    for (int i = 0; i < 5; i++) DELAY_REGS[i] = s_entryDelay[i];
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_BEFORE_EACH(load_tests, testname, testindex,
)

CESTER_AFTER_EACH(load_tests, testname, testindex,
)

/* Every number below is a cost measured under one cache configuration, and the
   suite never set it - it inherits whatever the runtime left in BIU_CONFIG. On
   hardware that is 0x0001e988 (scratchpad enabled, RAM enabled), which is the
   regime the figures describe. Assert it rather than assume it: a different
   value means the rest of this file is measuring something else. */
CESTER_TEST(biuConfigIsTheMeasuredRegime, load_tests,
    uint32_t biu = *(volatile uint32_t *)BIU_CONFIG_ADDR;
    ramsyscall_printf("=== BIU_CONFIG = 0x%08lx (expected 0x%08lx) ===\n",
                      biu, (uint32_t)BIU_EXPECTED);
    cester_assert_uint_eq((uint32_t)BIU_EXPECTED, biu);
)

/* Back-to-back load cost per target: scratchpad ~1, MMIO ~5, RAM ~7, BIOS tens
   (model-dependent). Only the ordering and the on-die value are asserted. */
CESTER_MAYBE_TEST(loadCostByTarget, load_tests,
    BENCH(base, timed_nop,  (volatile void *)ADDR_ISTAT);
    BENCH(scratch, timed_read, (volatile void *)ADDR_SCRATCH);
    BENCH(mmio, timed_read, (volatile void *)ADDR_ISTAT);
    BENCH(ram_c, timed_read, (volatile void *)ADDR_RAM_C);
    BENCH(ram_u, timed_read, (volatile void *)ADDR_RAM_U);
    BENCH(bios, timed_read, (volatile void *)ADDR_BIOS);

    ramsyscall_printf("=== load cost by target (N=%d back-to-back) ===\n", N_READS);
    ramsyscall_printf("  nop baseline raw256=%u\n", base);
    report("scratchpad :", scratch, base);
    report("MMIO I_STAT:", mmio, base);
    report("RAM cached :", ram_c, base);
    report("RAM uncachd:", ram_u, base);
    report("BIOS ROM   :", bios, base);

    /* Scratchpad is single-cycle - as cheap as a nop, no bus involved. */
    cester_assert_true(scratch <= base + (uint32_t)N_READS / 2u);
    /* Strict cost ordering across the hierarchy. */
    cester_assert_true(scratch < mmio);
    cester_assert_true(mmio < ram_c);
    cester_assert_true(ram_c < bios);
    /* No data cache: cached and uncached main RAM cost the same (within jitter). */
    uint32_t ram_diff = ram_c > ram_u ? ram_c - ram_u : ram_u - ram_c;
    cester_assert_true(ram_diff <= (uint32_t)N_READS / 4u);
    /* On-die MMIO read ~5 cyc (4 marginal); band 3..5. */
    cester_assert_true((mmio - base) >= (uint32_t)N_READS * 3u &&
                       (mmio - base) <= (uint32_t)N_READS * 5u);
)

/* Main RAM sits behind a page-mode DRAM controller: two loads landing in the
   same row are a page hit, two in different rows force a precharge and a fresh
   activate. Sweeping the distance between two alternating load addresses should
   therefore step at the row size, which is the only way to get the array
   geometry out of software - the CPU-to-DRAM address lines are permuted (psx-spx
   records RAM.A11 wired to the chips' A8, with RAM.A8 and RAM.A10 unconnected on
   at least some retail consoles), so it cannot be read off the address map.
   Print-only: a pass threshold authored before the first hardware run is an
   assumption wearing a measurement's clothes. The single assertion below is an
   instrument check, not a claim about the phenomenon. */
CESTER_MAYBE_TEST(loadCostByStride, load_tests,
    volatile char *base = (volatile char *)ADDR_RAM_U;
    BENCH(ref, timed_read, (volatile void *)ADDR_RAM_U);

    ramsyscall_printf("=== load cost by stride (128 alternating pairs, uncached main RAM) ===\n");
    ramsyscall_printf("  Format: STRIDE <bytes> min=<ticks per 256 loads> max=<ticks> abs=<cyc/read>\n");
    ramsyscall_printf("  ref256=%u (single address, same block size)\n", ref);

    uint32_t stride0 = 0;
    for (int k = -1; k < 20; k++) {
        /* k < 0 is the stride-0 control. Strides must stay 4-byte aligned: an
           unaligned lw raises an address error, which is exactly how the first
           run of this test died one line in. */
        if (k >= 0 && k < 2) continue;
        uint32_t stride = (k < 0) ? 0u : (1u << k);
        volatile void *a = (volatile void *)base;
        volatile void *b = (volatile void *)(base + stride);
        uint32_t best = 0xffffu, worst = 0u;
        for (int i = 0; i < 8; i++) {
            uint32_t d = timed_pair(a, b);
            if (d < best) best = d;
            if (d > worst) worst = d;
        }
        if (k < 0) stride0 = best;
        uint32_t cc = (best * 100u + N_READS / 2) / N_READS;
        ramsyscall_printf("  STRIDE %u min=%u max=%u abs=%u.%02u\n", stride, best, worst, cc / 100u,
                          cc % 100u);
    }

    /* Alternating an address with itself must cost the same as 256 back-to-back
       reads of it. If this fails, the sweep is measuring something other than
       what it claims and none of the numbers above mean anything. */
    uint32_t d0 = stride0 > ref ? stride0 - ref : ref - stride0;
    cester_assert_true(d0 <= (uint32_t)N_READS / 8u);
)

/* Row-open timeout probe. The stride sweep held the inter-access gap constant at
   a few cycles, so if the controller holds rows open with a timeout, every
   sample in it was a page hit and it never generated a miss. This varies the gap
   instead, with the address fixed. A step in the load arm marks the timeout;
   a flat profile means page state is not observable in CPU-side load cost at
   all, whatever the bus is doing. Print-only, for the same reason as above. */
CESTER_MAYBE_TEST(loadCostByGap, load_tests,
    volatile void *A = (volatile void *)ADDR_RAM_U;

    ramsyscall_printf("=== load cost by inter-access gap (32 reps, one uncached RAM address) ===\n");
    ramsyscall_printf("  Format: GAP d=<delay loop count> load=<ticks> nop=<ticks> delta=<ticks per 32 loads>\n");

    for (uint32_t d = 1; d <= 48; d++) {
        uint32_t bl = 0xffffu, bn = 0xffffu;
        for (int i = 0; i < 8; i++) {
            uint32_t x = gap_load(A, d);
            uint32_t y = gap_nop(A, d);
            if (x < bl) bl = x;
            if (y < bn) bn = y;
        }
        ramsyscall_printf("  GAP d=%u load=%u nop=%u delta=%u\n", d, bl, bn,
                          bl > bn ? bl - bn : 0u);
    }
)

/* Loads cost the same through both mirrors because there is no data cache.
   Stores should not: the write queue buffers them through KSEG0 and is bypassed
   through KSEG1. */
CESTER_MAYBE_TEST(storeCostByMirror, load_tests,
    /* Sequential, not repeated: 256 stores to one address can be coalesced and
       tell you nothing about queue depth. This walks 1 KB. */
    BENCH(base, timed_walk, (volatile void *)ADDR_RAM_C);
    BENCH(w_c, timed_write, (volatile void *)ADDR_RAM_C);
    BENCH(w_u, timed_write, (volatile void *)ADDR_RAM_U);

    BENCH(nb, timed_nop, (volatile void *)ADDR_ISTAT);
    BENCH(b_c, timed_burst, (volatile void *)ADDR_RAM_C);
    BENCH(b_u, timed_burst, (volatile void *)ADDR_RAM_U);

    ramsyscall_printf("=== store cost by mirror (N=%d sequential words) ===\n", N_READS);
    ramsyscall_printf("  pointer-walk baseline raw256=%u\n", base);
    report("store KSEG0:", w_c, base);
    report("store KSEG1:", w_u, base);
    ramsyscall_printf("  --- saturating burst, 1 instr/store, nop baseline raw256=%u ---\n", nb);
    report("burst KSEG0:", b_c, nb);
    report("burst KSEG1:", b_u, nb);
)

/* Queue-depth sweep: N KSEG0 primers then one KSEG1 store. Under the
   synchronize model the KSEG1 store's cost rises with N and flattens once N
   reaches the queue depth. Print-only; the knee is the measurement. */
CESTER_MAYBE_TEST(kseg1StoreSync, load_tests,
    volatile void *C = (volatile void *)ADDR_RAM_C;
    volatile void *U = (volatile void *)ADDR_RAM_U;

    ramsyscall_printf("=== KSEG1 store sync vs primed queue depth (16 reps) ===\n");
    ramsyscall_printf("  Format: PRIME n=<KSEG0 stores ahead> sync=<ticks> base=<ticks> delta=<ticks per 16>\n");

    primeRow(0, prime0, pbase0, C, U);
    primeRow(1, prime1, pbase1, C, U);
    primeRow(2, prime2, pbase2, C, U);
    primeRow(3, prime3, pbase3, C, U);
    primeRow(4, prime4, pbase4, C, U);
    primeRow(5, prime5, pbase5, C, U);
    primeRow(6, prime6, pbase6, C, U);
)

/* Does a load of an address still in the 4-deep write queue get answered by the
   queue? If so, read-after-write is cheaper than reading a never-written
   address, and on whichever mirror bypasses the queue it is not. This decides
   whether a moving-inversions RAM test would be testing the array or the queue. */
CESTER_MAYBE_TEST(readAfterWrite, load_tests,
    volatile void *c0 = (volatile void *)ADDR_RAM_C;
    volatile void *c1 = (volatile void *)(ADDR_RAM_C + 4096u);
    volatile void *u0 = (volatile void *)ADDR_RAM_U;
    volatile void *u1 = (volatile void *)(ADDR_RAM_U + 4096u);

    uint32_t sc = 0xffffu, oc = 0xffffu, su = 0xffffu, ou = 0xffffu, nl = 0xffffu;
    for (int i = 0; i < 8; i++) {
        uint32_t a = raw_same(c0, c1);   if (a < sc) sc = a;
        uint32_t b = raw_other(c0, c1);  if (b < oc) oc = b;
        uint32_t c = raw_same(u0, u1);   if (c < su) su = c;
        uint32_t d = raw_other(u0, u1);  if (d < ou) ou = d;
        uint32_t e = raw_noload(c0, c1); if (e < nl) nl = e;
    }

    ramsyscall_printf("=== read-after-write (32 reps, interlocked) ===\n");
    ramsyscall_printf("  no-load baseline=%u\n", nl);
    ramsyscall_printf("  KSEG0 same=%u other=%u diff=%d\n", sc, oc, (int)oc - (int)sc);
    ramsyscall_printf("  KSEG1 same=%u other=%u diff=%d\n", su, ou, (int)ou - (int)su);
)

/* One on-die decoder, one latency: every on-die MMIO register reads the same. */
CESTER_MAYBE_TEST(onDieUniformity, load_tests,
    BENCH(istat, timed_read, (volatile void *)ADDR_ISTAT);
    BENCH(dma, timed_read, (volatile void *)ADDR_DMA);
    BENCH(rcnt0, timed_read, (volatile void *)&COUNTERS[0].value);
    BENCH(rcnt2, timed_read, (volatile void *)&COUNTERS[2].value);

    ramsyscall_printf("=== on-die uniformity ===\n");
    ramsyscall_printf("  I_STAT=%u DMA=%u RCNT0=%u RCNT2=%u\n", istat, dma, rcnt0, rcnt2);

    cester_assert_uint_eq(istat, dma);
    cester_assert_uint_eq(istat, rcnt0);
    cester_assert_uint_eq(istat, rcnt2);
)

/* Load shadow: an uncached load partially overlaps following independent
   instructions - neither a full stall nor fully hidden. */
CESTER_MAYBE_TEST(loadShadow, load_tests,
    volatile void *M = (volatile void *)ADDR_ISTAT;
    BENCH(s0, spaced0, M);
    BENCH(s1, spaced1, M);
    BENCH(s2, spaced2, M);
    BENCH(s3, spaced3, M);
    BENCH(s4, spaced4, M);
    BENCH(s5, spaced5, M);
    BENCH(s6, spaced6, M);
    BENCH(s7, spaced7, M);

    ramsyscall_printf("=== load-shadow sweep (%d loads + s trailing nops on MMIO) ===\n", N_LOADS);
    ramsyscall_printf("  s0=%u s1=%u s2=%u s3=%u s4=%u s5=%u s6=%u s7=%u\n", s0, s1, s2, s3, s4, s5,
                      s6, s7);

    cester_assert_uint_eq(164, s0);
    cester_assert_uint_eq(196, s1);
    cester_assert_uint_eq(228, s2);
    cester_assert_uint_eq(228, s3);
    cester_assert_uint_eq(228, s4);
    cester_assert_uint_eq(260, s5);
    cester_assert_uint_eq(292, s6);
    cester_assert_uint_eq(324, s7);
)

CESTER_MAYBE_TEST(loadInterlocked, load_tests,
    volatile void *M = (volatile void *)ADDR_ISTAT;
    BENCH(s1, intlck1, M);
    BENCH(s2, intlck2, M);
    BENCH(s3, intlck3, M);
    BENCH(s4, intlck4, M);
    BENCH(s5, intlck5, M);
    BENCH(s6, intlck6, M);
    BENCH(s7, intlck7, M);

    ramsyscall_printf("=== load-interlocked sweep (%d loads + 1 addiu after s trailing nops on MMIO) ===\n", N_LOADS);
    ramsyscall_printf("  s1=%u s2=%u s3=%u s4=%u s5=%u s6=%u s7=%u\n", s1, s2, s3, s4, s5,
                      s6, s7);

    cester_assert_uint_eq(420, s1);
    cester_assert_uint_eq(420, s2);
    cester_assert_uint_eq(388, s3);
    cester_assert_uint_eq(356, s4);
    cester_assert_uint_eq(356, s5);
    cester_assert_uint_eq(356, s6);
    cester_assert_uint_eq(356, s7);
)

/* The shadow sweep, for every kind of target (by stenzek). Each SHADOW row is
   16 loads with s nops after each one, and the cyc line is what a load costs
   over a nop in its place, so it is the access time for small s, and whatever
   part of the access always holds the CPU up once s is past the end of the
   shadow.

   Every bus target falls by about a cycle per added nop to a floor of 2 cycles
   a load: on-die MMIO by s=4, RAM by s=6, CD-ROM by s=9, SPU by s=20, ROM word
   loads by s=32. The scratchpad reads the same as the nop arm throughout. The
   first few points are an eighth of a cycle under, as nothing comes after the
   last load of the sixteen.

   Both the load row and the nop row are asserted to the tick. The ROM, CD-ROM
   and SPU rows hold only with the delay registers pinned. I_STAT at s=8 also
   has to come out at the 2 cycles a load the earlier sweep settled on, or the
   blocks here are not measuring what MAKE_SPACED does. */
CESTER_MAYBE_TEST(loadShadowByTarget, load_tests,
    ramsyscall_printf("=== load-shadow sweep by target (%d loads + s trailing nops) ===\n", SHADOW_REPS);
    ramsyscall_printf("  Format: <s>=<value>. ticks = load arm, base = nop arm, cyc = (ticks - base) / %d\n",
                      SHADOW_REPS);

    uint32_t a[SHADOW_COUNT], b[SHADOW_COUNT];
    uint32_t check = 0;
    for (unsigned t = 0; t < SWEEP_TARGETS; t++) {
        const SweepTarget *st = &s_sweepTargets[t];
        volatile void *M = (volatile void *)st->addr;
        uint32_t c = st->byte ? shadowSweepB(st->name, M, a, b) : shadowSweepW(st->name, M, a, b);
        if (t == 0) check = c;
        /* Counted first: the assert evaluates its arguments more than once. */
        unsigned bad = sweepMismatches("SHADOW", "s", st, s_shadowPoints, SHADOW_COUNT, a, st->shadow, 0);
        unsigned badNop = sweepMismatches("SHADOW", "nop s", st, s_shadowPoints, SHADOW_COUNT, b, s_shNop, 0);
        cester_assert_uint_eq(0, bad);
        cester_assert_uint_eq(0, badNop);
    }

    cester_assert_uint_eq((uint32_t)SHADOW_REPS * 2u, check);
)

/* The interlocked sweep, for every kind of target (by stenzek). Each INTLCK
   row is 8 loads, with the result read k instructions after each one, and the
   cyc line is what reading it there costs over not reading it at all. Zero
   means the load had completed by then, so the first k that reads zero is the
   length of the shadow, and what is read before it is how much of the access
   can be hidden.

   k=1 is the load delay slot. It reads the register's old value, so it should
   not wait for the load, whatever the target. k=1..3 are flat, then the stall
   drops by a cycle per instruction: 2 cycles at k<=3 for on-die MMIO, 4 for
   RAM, 24 for a ROM word, 6 for a ROM byte, 7 for CD-ROM, 17 for SPU, none for
   the scratchpad. Asserted to the tick, with the base arm. */
CESTER_MAYBE_TEST(loadInterlockedByTarget, load_tests,
    ramsyscall_printf("=== load-interlocked sweep by target (%d loads, result read k instructions later) ===\n",
                      INTLCK_REPS);
    ramsyscall_printf("  Format: <k>=<value>. ticks = result read, base = result not read, cyc = (ticks - base) / %d\n",
                      INTLCK_REPS);

    uint32_t a[INTLCK_COUNT], b[INTLCK_COUNT];
    for (unsigned t = 0; t < SWEEP_TARGETS; t++) {
        const SweepTarget *st = &s_sweepTargets[t];
        volatile void *M = (volatile void *)st->addr;
        if (st->byte) intlckSweepB(st->name, M, a, b);
        else intlckSweepW(st->name, M, a, b);
        unsigned bad = sweepMismatches("INTLCK", "k", st, s_intlckPoints, INTLCK_COUNT, a, st->intlck, 0);
        unsigned badBase = sweepMismatches("INTLCK", "base k", st, s_intlckPoints, INTLCK_COUNT, b, 0,
                                           st->intlckBase);
        cester_assert_uint_eq(0, bad);
        cester_assert_uint_eq(0, badBase);
    }
)

/* Bus register values at program entry: the BIOS plus whatever the loader
   left, then the same registers as the suite runs with them, the delay
   registers pinned. Print-only; the sweep below is relative to the latter.
   The pin is checked: it holds through every test before this one. */
CESTER_TEST(busRegistersAtEntry, load_tests,
    ramsyscall_printf("=== bus registers at entry ===\n");
    for (uint32_t a = 0xbf801000u; a <= 0xbf801020u; a += 4u) {
        uint32_t now = *(volatile uint32_t *)a;
        uint32_t was = a >= 0xbf801010u ? s_entryDelay[(a - 0xbf801010u) / 4u] : now;
        ramsyscall_printf("  REG %08x = %08x now %08x\n", a & 0x1fffffffu, was, now);
    }
    ramsyscall_printf("  REG 1f801060 = %08x\n", *(volatile uint32_t *)0xbf801060u);
    cester_assert_uint_eq(PIN_COM_DELAY, COM_DELAY);
    cester_assert_uint_eq(PIN_CDROM_DELAY, CDROM_DELAY);
)

/* Which delay/size bit and which COM_DELAY nibble moves the cost of which
   access, on this console. Each arm is the entry value of the device register
   with one change: bit 8, 9, 10 or 11 toggled alone, read or write delay
   forced to 0 or 15, bits 8-11 all set, or one COM nibble set to 0, 5 or 15
   with its enabling bit set. The last arm repeats the first: a restore or
   instrument fault shows up as the two differing. Raw ticks are min/max over
   8 runs of N_BUS accesses. Stores are timed as issued, so the last few may
   still sit in the write queue when the counter is read; that tail is the
   same in every arm. Stores also vary by a few ticks run to run, so the
   repeat check on DEV4 is banded. The exact values asserted are the two arms
   that use no COM_DELAY nibble at all (DEV2 with bit 10 cleared, DEV4 with
   bit 8 cleared, from the BIOS values 0013243Fh and 200931E1h): those
   reproduce to the tick on every console tested, while the other arms follow
   COM_DELAY, which the suite pins. The BUS5 arms then set CDROM_DELAY bit 8
   and COM_DELAY to the values a boot with and without a disc leaves, and time
   byte loads from the CD-ROM controller and the SPU; those are asserted to
   the tick on the min. */
CESTER_MAYBE_TEST(busDelaySweep, load_tests,
    volatile void *rom = (volatile void *)ADDR_BIOS;
    volatile void *spu = (volatile void *)ADDR_SPU_ADSR;
    uint32_t dev2 = DEV2_DELAY, dev4 = DEV4_DELAY, com = COM_DELAY;
    uint32_t nlo, nhi;
    RUN8(nlo, nhi, bus_nop(rom));
    ramsyscall_printf("=== bus delay sweep (N=%d, entry dev2=%08x dev4=%08x com=%08x) ===\n", N_BUS,
                      dev2, dev4, com);
    ramsyscall_printf("  Format: BUSn <arm> dev=<reg> com=<reg> <access>=<min>/<max> ...\n");
    ramsyscall_printf("  BUSNOP nop=%u/%u\n", nlo, nhi);

    int subjects = 0;
    uint32_t first2 = 0, last2 = 0, first4 = 0, last4 = 0;
    uint32_t nocom2[3] = {0, 0, 0}, nocom4 = 0;
    int n = (int)(sizeof(s_busArms) / sizeof(s_busArms[0]));
    for (int d = 0; d < 2; d++) {
        volatile uint32_t *reg = d == 0 ? &DEV2_DELAY : &DEV4_DELAY;
        uint32_t v0 = *reg;
        for (int k = 0; k < n; k++) {
            const BusArm *arm = &s_busArms[k];
            uint32_t cfg = ((v0 & ~arm->clr) | arm->set) ^ arm->flip;
            uint32_t c = com;
            if (arm->nib >= 0) c = (com & ~(0xfu << (4 * arm->nib))) | (arm->val << (4 * arm->nib));
            uint32_t alo, ahi, blo, bhi, clo, chi, dlo = 0, dhi = 0;
            COM_DELAY = c;
            *reg = cfg;
            if (d == 0) {
                RUN8(alo, ahi, bus_lw(rom));
                RUN8(blo, bhi, bus_lh(rom));
                RUN8(clo, chi, bus_lb(rom));
            } else {
                RUN8(alo, ahi, bus_sh(spu));
                RUN8(blo, bhi, bus_sw(spu));
                RUN8(clo, chi, bus_lhs(spu));
                RUN8(dlo, dhi, bus_lws(spu));
            }
            *reg = v0;
            COM_DELAY = com;
            if (d == 0) {
                ramsyscall_printf("  BUS2 %-8s dev=%08x com=%08x lw=%u/%u lh=%u/%u lb=%u/%u\n", arm->name, cfg,
                                  c, alo, ahi, blo, bhi, clo, chi);
                if (k == 0) first2 = alo;
                if (k == 3) { nocom2[0] = alo; nocom2[1] = blo; nocom2[2] = clo; }
                last2 = alo;
            } else {
                ramsyscall_printf("  BUS4 %-8s dev=%08x com=%08x sh=%u/%u sw=%u/%u lh=%u/%u lw=%u/%u\n", arm->name,
                                  cfg, c, alo, ahi, blo, bhi, clo, chi, dlo, dhi);
                if (k == 0) first4 = blo;
                if (k == 1) nocom4 = dlo;
                last4 = blo;
            }
            subjects++;
        }
    }

    /* CD-ROM delay bit 8 x COM_DELAY, byte loads from the CD-ROM controller
       and the SPU. */
    uint32_t cd = CDROM_DELAY;
    int ncd = (int)(sizeof(s_cdArms) / sizeof(s_cdArms[0]));
    uint32_t cdlo[sizeof(s_cdArms) / sizeof(s_cdArms[0])], splo[sizeof(s_cdArms) / sizeof(s_cdArms[0])];
    for (int k = 0; k < ncd; k++) {
        const CdArm *arm = &s_cdArms[k];
        uint32_t clo, chi, slo, shi;
        COM_DELAY = arm->com;
        CDROM_DELAY = arm->cd;
        RUN8(clo, chi, bus_lbu((volatile void *)ADDR_CDROM));
        RUN8(slo, shi, bus_lbu((volatile void *)ADDR_SPU));
        CDROM_DELAY = cd;
        COM_DELAY = com;
        ramsyscall_printf("  BUS5 cd=%08x com=%08x lbu-cdrom=%u/%u lbu-spu=%u/%u\n", arm->cd, arm->com, clo, chi,
                          slo, shi);
        cdlo[k] = clo;
        splo[k] = slo;
        subjects++;
    }
    ramsyscall_printf("  BUS subjects=%d\n", subjects);

    cester_assert_int_eq(2 * n + ncd, subjects);
    cester_assert_uint_eq(dev2, DEV2_DELAY);
    cester_assert_uint_eq(dev4, DEV4_DELAY);
    cester_assert_uint_eq(com, COM_DELAY);
    cester_assert_uint_eq(cd, CDROM_DELAY);
    cester_assert_uint_eq(first2, last2);
    uint32_t d4 = first4 > last4 ? first4 - last4 : last4 - first4;
    cester_assert_true(d4 <= (uint32_t)N_BUS / 8u);
    if (dev2 == 0x0013243fu) {
        cester_assert_uint_eq(1542, nocom2[0]);
        cester_assert_uint_eq(902, nocom2[1]);
        cester_assert_uint_eq(582, nocom2[2]);
    }
    if (dev4 == 0x200931e1u) cester_assert_uint_eq(2310, nocom4);
    /* Bit 8 clear: 10 cycles a load whatever COM_DELAY is. Bit 8 set: 11 at
       00031125h, ~17.9 at 0000132Ch. The SPU follows COM_DELAY alone. These
       arms set both registers, so they hold on every console. */
    for (int k = 0; k < ncd; k++) {
        cester_assert_uint_eq(s_cdArms[k].cdrom, cdlo[k]);
        cester_assert_uint_eq(s_cdArms[k].spu, splo[k]);
    }
)

CESTER_OPTIONS(
    CESTER_VERBOSE();
)
