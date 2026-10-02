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

// Regression test for pcsx-redux#1928: LWL/LWR/SWL/SWR access the aligned word containing
// their address, but msan must only consider the bytes the instruction consumes or overwrites.
// Any msan violation makes the emulator exit with a non-zero code in test mode.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

// The emulator reads the size from $a0 when handling the msan allocation register. Keep this
// out of line so the dynarec has $a0 written back to the guest registers at that point.
CESTER_BODY(
static __attribute__((noipa)) void *msanAlloc(uint32_t size) { return pcsx_msanAlloc(size); }
)

CESTER_BEFORE_ALL(msan_tests,
    pcsx_initMsan();
)

// SWL then LWL over the same two bytes of a fresh word, at bitmap offsets 0 and 4.
CESTER_TEST(msan_swl_lwl, msan_tests,
    uint8_t *p = msanAlloc(8);
    uint32_t v = 0x11223344;
    uint32_t r0 = 0, r1 = 0;
    __asm__ volatile("swl %0, 1(%1)" : : "r"(v), "r"(p) : "memory");
    __asm__ volatile("swl %0, 5(%1)" : : "r"(v), "r"(p) : "memory");
    __asm__ volatile("lwl %0, 1(%1)" : "+r"(r0) : "r"(p) : "memory");
    __asm__ volatile("lwl %0, 5(%1)" : "+r"(r1) : "r"(p) : "memory");
    cester_assert_uint_eq(0x11220000, r0);
    cester_assert_uint_eq(0x11220000, r1);
    cester_assert_uint_eq(0x22, p[0]);
    cester_assert_uint_eq(0x11, p[1]);
    pcsx_msanFree(p);
)

// SWR then LWR over the top three bytes of a fresh word, at bitmap offsets 0 and 4.
CESTER_TEST(msan_swr_lwr, msan_tests,
    uint8_t *p = msanAlloc(8);
    uint32_t v = 0x11223344;
    uint32_t r0 = 0, r1 = 0;
    __asm__ volatile("swr %0, 1(%1)" : : "r"(v), "r"(p) : "memory");
    __asm__ volatile("swr %0, 5(%1)" : : "r"(v), "r"(p) : "memory");
    __asm__ volatile("lwr %0, 1(%1)" : "+r"(r0) : "r"(p) : "memory");
    __asm__ volatile("lwr %0, 5(%1)" : "+r"(r1) : "r"(p) : "memory");
    cester_assert_uint_eq(0x00223344, r0);
    cester_assert_uint_eq(0x00223344, r1);
    cester_assert_uint_eq(0x44, p[5]);
    cester_assert_uint_eq(0x22, p[7]);
    pcsx_msanFree(p);
)

// LWR consuming only the half a SH initialized.
CESTER_TEST(msan_sh_lwr, msan_tests,
    uint8_t *p = msanAlloc(8);
    uint32_t r = 0;
    *(volatile uint16_t *)(p + 6) = 0xbeef;
    __asm__ volatile("lwr %0, 6(%1)" : "+r"(r) : "r"(p) : "memory");
    cester_assert_uint_eq(0xbeef, r);
    pcsx_msanFree(p);
)

// Unaligned word store and load with the usual pairs, into a 13-byte allocation: the
// bytes of the last word the SWL preserves are not usable, which must not matter.
CESTER_TEST(msan_unaligned_pair, msan_tests,
    uint8_t *p = msanAlloc(13);
    uint8_t *q = p + 9;
    uint32_t v = 0xcafef00d;
    uint32_t r = 0;
    __asm__ volatile("swl %0, 3(%1)\n swr %0, 0(%1)" : : "r"(v), "r"(q) : "memory");
    __asm__ volatile("lwl %0, 3(%1)\n lwr %0, 0(%1)" : "+r"(r) : "r"(q) : "memory");
    cester_assert_uint_eq(v, r);
    cester_assert_uint_eq(0xca, p[12]);
    pcsx_msanFree(p);
)
