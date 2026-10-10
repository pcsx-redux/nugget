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
 * cpu/gte/geometrytransformationenginegte.md says GTE load and store
 * instructions have "a delay of 2 instructions, for any GTE commands or
 * operations accessing that register. Any? That's wrong!". Each arm writes
 * an OLD value to a GTE register (then 4 nops), writes a NEW value, runs N
 * nops (N = 0..4), then uses the register. The printed value says which one
 * the use saw. Interrupts are off and the code runs from the cache-warm
 * second pass.
 *
 *   SQR_M  mtc2 IR1,         N nops, cop2 SQR, mfc2 IR1   old 2 -> 4, new 3 -> 9
 *   SQR_L  lwc2 IR1,         N nops, cop2 SQR, mfc2 IR1   same values
 *   OP_C   ctc2 RT22 (D2),   N nops, cop2 OP,  mfc2 MAC1  IR2=0 IR3=1: old 5, new 7
 *   RD_M   mtc2 IR1,         N nops, mfc2 IR1             old 2, new 3
 *   RD_C   ctc2 RT22RT23,    N nops, cfc2 RT22RT23        old 5, new 7
 *   SW_M   mtc2 IR1,         N nops, swc2 IR1             old 2, new 3
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

static volatile uint32_t s_mem[2];

#define PRE_D(reg, v) "li $t0, " #v "\nmtc2 $t0, $" #reg "\nnop\nnop\nnop\nnop\n"
#define PRE_C(reg, v) "li $t0, " #v "\nctc2 $t0, $" #reg "\nnop\nnop\nnop\nnop\n"
#define NOPS(n) ".rept " #n "\nnop\n.endr\n"

#define ARMS(N)                                                                                         \
    static uint32_t sqrM##N(void) {                                                                     \
        uint32_t r;                                                                                     \
        __asm__ volatile(".set push\n.set noreorder\n" PRE_D(9, 2) "li $t0, 3\nmtc2 $t0, $9\n" NOPS(N) \
                         "cop2 0xa00028\nmfc2 %0, $9\nnop\n.set pop"                                    \
                         : "=r"(r) : : "t0");                                                           \
        return r;                                                                                       \
    }                                                                                                   \
    static uint32_t sqrL##N(void) {                                                                     \
        uint32_t r;                                                                                     \
        s_mem[0] = 3;                                                                                   \
        __asm__ volatile(".set push\n.set noreorder\n" PRE_D(9, 2) "lwc2 $9, 0(%1)\n" NOPS(N)           \
                         "cop2 0xa00028\nmfc2 %0, $9\nnop\n.set pop"                                    \
                         : "=r"(r) : "r"(&s_mem[0]) : "t0");                                            \
        return r;                                                                                       \
    }                                                                                                   \
    static uint32_t opC##N(void) {                                                                      \
        uint32_t r;                                                                                     \
        __asm__ volatile(".set push\n.set noreorder\n" PRE_D(10, 0) PRE_D(11, 1) PRE_C(2, 5)            \
                         "li $t0, 7\nctc2 $t0, $2\n" NOPS(N) "cop2 0x170000c\nmfc2 %0, $25\nnop\n.set pop" \
                         : "=r"(r) : : "t0");                                                           \
        return r;                                                                                       \
    }                                                                                                   \
    static uint32_t rdM##N(void) {                                                                      \
        uint32_t r;                                                                                     \
        __asm__ volatile(".set push\n.set noreorder\n" PRE_D(9, 2) "li $t0, 3\nmtc2 $t0, $9\n" NOPS(N) \
                         "mfc2 %0, $9\nnop\n.set pop"                                                   \
                         : "=r"(r) : : "t0");                                                           \
        return r;                                                                                       \
    }                                                                                                   \
    static uint32_t rdC##N(void) {                                                                      \
        uint32_t r;                                                                                     \
        __asm__ volatile(".set push\n.set noreorder\n" PRE_C(2, 5) "li $t0, 7\nctc2 $t0, $2\n" NOPS(N) \
                         "cfc2 %0, $2\nnop\n.set pop"                                                   \
                         : "=r"(r) : : "t0");                                                           \
        return r;                                                                                       \
    }                                                                                                   \
    static uint32_t swM##N(void) {                                                                      \
        s_mem[1] = 0xdead;                                                                              \
        __asm__ volatile(".set push\n.set noreorder\n" PRE_D(9, 2) "li $t0, 3\nmtc2 $t0, $9\n" NOPS(N) \
                         "swc2 $9, 0(%0)\nnop\n.set pop"                                                \
                         : : "r"(&s_mem[1]) : "t0", "memory");                                          \
        return s_mem[1];                                                                                \
    }

ARMS(0)
ARMS(1)
ARMS(2)
ARMS(3)
ARMS(4)

typedef uint32_t (*Arm)(void);
static const Arm s_arms[6][5] = {
    {sqrM0, sqrM1, sqrM2, sqrM3, sqrM4}, {sqrL0, sqrL1, sqrL2, sqrL3, sqrL4},
    {opC0, opC1, opC2, opC3, opC4},      {rdM0, rdM1, rdM2, rdM3, rdM4},
    {rdC0, rdC1, rdC2, rdC3, rdC4},      {swM0, swM1, swM2, swM3, swM4},
};
static const char* const s_names[6] = {"SQR_M", "SQR_L", "OP_C", "RD_M", "RD_C", "SW_M"};

int main(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"((sr & ~1u) | 0x40000000u));
    ramsyscall_printf("GSD-START\n");
    for (int rep = 0; rep < 3; rep++) {
        for (int a = 0; a < 6; a++) {
            uint32_t v[5];
            for (int n = 0; n < 5; n++) {
                (void)s_arms[a][n]();
                v[n] = s_arms[a][n]();
            }
            ramsyscall_printf("GSD rep=%d %s N0..4 = %x %x %x %x %x\n", rep, s_names[a], v[0], v[1], v[2], v[3], v[4]);
        }
    }
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
    ramsyscall_printf("GSD-DONE\n");
    return 0;
}
