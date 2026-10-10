/*

MIT License

Copyright (c) 2022 PCSX-Redux authors

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

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

#include "cester-cop0.c"

CESTER_TEST(cpu_cop0_basic_write_bp, cpu_tests,
    uint32_t expectedEPC;
    uint32_t t;
    volatile uint32_t * ptr = (volatile uint32_t *) 0x58;
    *ptr = 1;
    __asm__ volatile(""
"    lui   %0, 0b1100101010000000\n"
"    mtc0  %0, $7\n"
"    li    %0, 0x58\n"
"    mtc0  %0, $5\n"
"    li    %0, 0xfffffff0\n"
"    mtc0  %0, $9\n" : "=r"(t));

    cester_assert_uint_eq(1, *ptr);

    __asm__ volatile("la %0, 1f\n1:\nsw $0, 0x58($0)" : "=r"(expectedEPC));

    __asm__ volatile("mtc0 $0, $7\n");

    cester_assert_uint_eq(0, *ptr);
    cester_assert_uint_eq(1, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0x40, s_from);
    cester_assert_uint_eq(expectedEPC, s_epc);
)

CESTER_TEST(cpu_cop0_kseg_write_bp, cpu_tests,
    uint32_t t;
    volatile uint32_t * ptr = (volatile uint32_t *) 0x80000058;
    *ptr = 1;
    __asm__ volatile(""
"    lui   %0, 0b1100101010000000\n"
"    mtc0  %0, $7\n"
"    li    %0, 0x58\n"
"    mtc0  %0, $5\n"
"    li    %0, 0xfffffff0\n"
"    mtc0  %0, $9\n" : "=r"(t));

    cester_assert_uint_eq(1, *ptr);

    __asm__ volatile("lui $at, 0x8000\nsw $0, 0x58($at)");

    __asm__ volatile("mtc0 $0, $7\n");

    cester_assert_uint_eq(0, *ptr);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
)

CESTER_TEST(cpu_cop0_upper_memory_write_bp, cpu_tests,
    uint32_t t;
    volatile uint32_t * ptr = (volatile uint32_t *) 0x00200058;
    *ptr = 1;
    __asm__ volatile(""
"    lui   %0, 0b1100101010000000\n"
"    mtc0  %0, $7\n"
"    li    %0, 0x58\n"
"    mtc0  %0, $5\n"
"    li    %0, 0xfffffff0\n"
"    mtc0  %0, $9\n" : "=r"(t));

    cester_assert_uint_eq(1, *ptr);

    __asm__ volatile("lui $at, 0x0020\nsw $0, 0x58($at)");

    __asm__ volatile("mtc0 $0, $7\n");

    cester_assert_uint_eq(0, *ptr);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
)

CESTER_TEST(cpu_cop0_unaligned_write_bp, cpu_tests,
    uint32_t expectedEPC;
    uint32_t t;
    volatile uint32_t * ptr = (volatile uint32_t *) 0x58;
    *ptr = 0x01020304;
    __asm__ volatile(""
"    lui   %0, 0b1100101010000000\n"
"    mtc0  %0, $7\n"
"    li    %0, 0x58\n"
"    mtc0  %0, $5\n"
"    li    %0, 0xfffffff0\n"
"    mtc0  %0, $9\n" : "=r"(t));

    cester_assert_uint_eq(0x01020304, *ptr);

    __asm__ volatile("la %0, 1f\n1:\nsb $0, 0x59($0)" : "=r"(expectedEPC));

    __asm__ volatile("mtc0 $0, $7\n");

    cester_assert_uint_eq(0x01020004, *ptr);
    cester_assert_uint_eq(1, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0x40, s_from);
    cester_assert_uint_eq(expectedEPC, s_epc);
)

CESTER_TEST(cpu_unaligned_write_fault, cpu_tests,
    uint32_t expectedEPC;
    __asm__ volatile("la %0, 1f\n1:\nsw $0, 1($0)" : "=r"(expectedEPC));
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(1, s_got80);
    cester_assert_uint_eq(0x80, s_from);
    cester_assert_uint_eq(expectedEPC, s_epc);
)

// SR keeps IEc..KUo, IM, IsC, SwC, PZ, CM, BEV and CU0..CU3; bits 6-7,
// 20-21 (PE, TS) and 23-27 (RE among them) read 0 after writing 1. KUc
// (bit 1) is left out here: setting it drops this code into user mode.
CESTER_MAYBE_TEST(cpu_cop0_sr_writable_bits, cpu_tests,
    uint32_t old, got, mask = 0;
    __asm__ volatile("mfc0 %0, $12\nnop" : "=r"(old));
    for (unsigned b = 0; b < 32; b++) {
        if (b == 1) continue;
        uint32_t v = 1u << b;
        __asm__ volatile(".set push\n.set noreorder\n"
                         "mtc0 %1, $12\nnop\nnop\nmfc0 %0, $12\nnop\nmtc0 %2, $12\nnop\nnop\n"
                         ".set pop" : "=&r"(got) : "r"(v), "r"(old));
        mask |= got & v;
    }
    cester_assert_uint_eq(0xf04fff3d, mask);
)

// With CU1 or CU3 clear, cop1 and cop3 opcodes raise Coprocessor Unusable
// even in kernel mode, with CE naming the coprocessor.
CESTER_MAYBE_TEST(cpu_cop0_cu1_unusable, cpu_tests,
    uint32_t old, sr;
    __asm__ volatile("mfc0 %0, $12\nnop" : "=r"(old));
    sr = old & ~0x20000000;
    __asm__ volatile("mtc0 %0, $12\nnop\nnop" : : "r"(sr));
    __asm__ volatile(".word 0x44080000" : : : "t0");
    __asm__ volatile("mtc0 %0, $12\nnop\nnop" : : "r"(old));
    cester_assert_uint_eq(1, s_got80);
    uint32_t ce = s_cause & 0x3000007c;
    cester_assert_uint_eq(0x1000002c, ce);
)

CESTER_MAYBE_TEST(cpu_cop0_cu3_unusable, cpu_tests,
    uint32_t old, sr;
    __asm__ volatile("mfc0 %0, $12\nnop" : "=r"(old));
    sr = old & ~0x80000000;
    __asm__ volatile("mtc0 %0, $12\nnop\nnop" : : "r"(sr));
    __asm__ volatile(".word 0x4c080000" : : : "t0");
    __asm__ volatile("mtc0 %0, $12\nnop\nnop" : : "r"(old));
    cester_assert_uint_eq(1, s_got80);
    uint32_t ce = s_cause & 0x3000007c;
    cester_assert_uint_eq(0x3000002c, ce);
)

// an exception raised in a taken branch's delay slot sets Cause.BD, and EPC
// points at the branch instead of the faulting instruction; the code is in
// ../cpu/branchbranch.s, next to the other branch delay slot oddities
CESTER_TEST(cpu_ADD_overflow_in_delay_slot, cpu_tests,
    s_resume = (uint32_t *)delayslot_resume;
    uint32_t branch = delayslot_add();
    uint32_t excode = (s_cause >> 2) & 0x1f;
    uint32_t bd = s_cause >> 31;
    cester_assert_uint_eq(1, s_got80);
    cester_assert_uint_eq(12, excode);
    cester_assert_uint_eq(1, bd);
    cester_assert_uint_eq(branch, s_epc);
)

CESTER_TEST(cpu_SYSCALL_in_delay_slot, cpu_tests,
    s_resume = (uint32_t *)delayslot_resume;
    uint32_t branch = delayslot_syscall();
    uint32_t excode = (s_cause >> 2) & 0x1f;
    uint32_t bd = s_cause >> 31;
    cester_assert_uint_eq(1, s_got80);
    cester_assert_uint_eq(8, excode);
    cester_assert_uint_eq(1, bd);
    cester_assert_uint_eq(branch, s_epc);
)

CESTER_TEST(cpu_BREAK_in_delay_slot, cpu_tests,
    s_resume = (uint32_t *)delayslot_resume;
    uint32_t branch = delayslot_break();
    uint32_t excode = (s_cause >> 2) & 0x1f;
    uint32_t bd = s_cause >> 31;
    cester_assert_uint_eq(1, s_got80);
    cester_assert_uint_eq(9, excode);
    cester_assert_uint_eq(1, bd);
    cester_assert_uint_eq(branch, s_epc);
)

// a bc2f would always branch if it could run, but with SR.CU2 clear it raises
// Coprocessor Unusable from the delay slot: the outer branch isn't taken
CESTER_TEST(cpu_BC2F_in_delay_slot_cu2_clear, cpu_tests,
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12\nnop" : "=r"(sr));
    s_resume = (uint32_t *)delayslot_resume;
    uint32_t branch = delayslot_bc2f();
    __asm__ volatile("mtc0 %0, $12\nnop" : : "r"(sr));
    uint32_t excode = (s_cause >> 2) & 0x1f;
    uint32_t ce = (s_cause >> 28) & 3;
    uint32_t bd = s_cause >> 31;
    cester_assert_uint_eq(1, s_got80);
    cester_assert_uint_eq(11, excode);
    cester_assert_uint_eq(2, ce);
    cester_assert_uint_eq(1, bd);
    cester_assert_uint_eq(branch, s_epc);
    cester_assert_uint_eq(0, delayslot_taken);
)
