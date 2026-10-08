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

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/hardware/counters.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

// A hardware interrupt landing on a branch that sits in the delay slot of a
// taken branch. See pair.s. Where silicon reports EPC/BD for this case is not
// measured yet: the OBS line below is there so a hardware run can tell.

#ifndef TIMER_TARGET
#define TIMER_TARGET 300
#endif
#ifndef IRQ_LIMIT
#define IRQ_LIMIT 1000
#endif
#define ITERATIONS 200000

CESTER_BODY(
    uint32_t pairLoop(uint32_t iterations);
    extern uint32_t pairStart[], pairB1[], pairB2[], pairT1[], pairEnd[];
    void installExceptionHandlers(uint32_t (*handler)(uint32_t * regs, uint32_t from));
    void uninstallExceptionHandlers();

    static volatile uint32_t s_irqs;
    static volatile uint32_t s_other;
    static volatile uint32_t s_atB2BD;     // EPC == B2, BD=1
    static volatile uint32_t s_atB2NoBD;   // EPC == B2, BD=0
    static volatile uint32_t s_atT1;       // EPC == T1
    static volatile uint32_t s_atB1;       // EPC == B1
    static volatile uint32_t s_atB1BD;     // EPC == B1, BD=1
    static volatile uint32_t s_inside;     // anywhere else in the routine
    static volatile uint32_t s_outside;
    static volatile uint32_t s_bdTotal;
    static uint32_t s_ref, s_irq, s_srAfter;

    static uint32_t handler(uint32_t * regs, uint32_t from) {
        uint32_t cause, epc;
        asm volatile("mfc0 %0, $13\nnop\nmfc0 %1, $14\nnop" : "=r"(cause), "=r"(epc));
        if ((cause & 0x7c) != 0) {
            s_other++;
            return epc + 4;
        }
        // acknowledge timer 2 (IRQ6) and clear its flags
        IREG = ~(1u << 6);
        (void)COUNTERS[2].mode;
        s_irqs++;
        int bd = (cause >> 31) & 1;
        s_bdTotal += bd;
        if (epc == (uint32_t)pairB2) {
            if (bd) s_atB2BD++; else s_atB2NoBD++;
        } else if (epc == (uint32_t)pairT1) {
            s_atT1++;
        } else if (epc == (uint32_t)pairB1) {
            if (bd) s_atB1BD++; else s_atB1++;
        } else if (epc >= (uint32_t)pairStart && epc <= (uint32_t)pairEnd + 4) {
            s_inside++;
        } else {
            s_outside++;
        }
        if (s_irqs >= IRQ_LIMIT) COUNTERS[2].mode = 0;
        // With BD=1, EPC is the address of the branch; RFE re-runs it.
        return epc;
    }

    static uint32_t getSR() {
        uint32_t sr;
        asm volatile("mfc0 %0, $12\nnop" : "=r"(sr));
        return sr;
    }
    static void setSR(uint32_t sr) { asm volatile("mtc0 %0, $12\nnop\nnop" : : "r"(sr)); }
)

CESTER_BEFORE_ALL(irq_branch_slot_tests,
    uint32_t oldSR = getSR();
    uint32_t oldIMASK = IMASK;
    setSR(oldSR & ~1u);
    IMASK = 0;
    IREG = 0;
    installExceptionHandlers(handler);
    syscall_flushCache();

    // reference: no interrupt
    s_ref = pairLoop(ITERATIONS);

    // now with a fast timer 2 IRQ, sysclock source, repeat mode
    COUNTERS[2].mode = 0;
    COUNTERS[2].value = 0;
    COUNTERS[2].target = TIMER_TARGET;
    IMASK = 1u << 6;
    IREG = 0;
    COUNTERS[2].mode = TM_RESET_TARGET | TM_IRQ_TARGET | TM_IRQ_REPEAT;
    setSR((oldSR & ~0x1fu) | 0x401u);
    s_irq = pairLoop(ITERATIONS);
    s_srAfter = getSR();
    setSR(getSR() & ~1u);
    COUNTERS[2].mode = 0;
    IMASK = 0;
    IREG = 0;
    uninstallExceptionHandlers();
    syscall_flushCache();
    IMASK = oldIMASK;
    setSR(oldSR);

    ramsyscall_printf("IRQBRANCHSLOT: OBS target=%d irqs=%d other=%d B2_bd=%d B2_nobd=%d T1=%d B1=%d B1_bd=%d inside=%d outside=%d bd_total=%d ref=0x%08x irq=0x%08x srAfter=0x%08x\n",
                      TIMER_TARGET, s_irqs, s_other, s_atB2BD, s_atB2NoBD, s_atT1, s_atB1, s_atB1BD, s_inside, s_outside,
                      s_bdTotal, s_ref, s_irq, s_srAfter);
)

CESTER_TEST(irq_branch_slot_irqs_handled, irq_branch_slot_tests,
    // every interrupt was taken, handled, and returned from with interrupts enabled
    cester_assert_uint_ge(s_irqs, IRQ_LIMIT);
    cester_assert_uint_eq(0, s_other);
    uint32_t iec = s_srAfter & 1;
    cester_assert_uint_eq(1, iec);
)

CESTER_MAYBE_TEST(irq_branch_slot_epc_on_inner_branch, irq_branch_slot_tests,
    // An IRQ taken right after B2 ran in B1's delay slot reports EPC = B2
    // with Cause.BD set.
    cester_assert_uint_ne(0, s_atB2BD);
)

CESTER_MAYBE_TEST(irq_branch_slot_reference_value, irq_branch_slot_tests,
    // B2 is relative to B1's target (see cpu_BRANCH_BRANCH_slot): it lands on
    // the loop's bnez, skipping T2, so each iteration adds only T1's 1.
    cester_assert_uint_eq(ITERATIONS, s_ref);
)

CESTER_MAYBE_TEST(irq_branch_slot_rerun_inner_branch, irq_branch_slot_tests,
    // Returning to EPC = B2 runs B2 on its own: relative to itself, through its
    // own delay slot (+1) to T2 (+0x100). Each such IRQ adds 0x100 over the
    // uninterrupted iteration.
    cester_assert_uint_eq(s_ref + 0x100 * s_atB2BD, s_irq);
)
