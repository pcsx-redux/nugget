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

#include "../cop0/cester-cop0.c"

CESTER_BODY(
    typedef uint32_t (*probe_t)();
    uint32_t copbranch_beq();
    uint32_t copbranch_bne();
    uint32_t copbranch_syscall();
    uint32_t copbranch_mfc0();
    uint32_t copbranch_rtpt_bc2f();
    uint32_t copbranch_rtpt_bc2t();
    uint32_t copbranch_flag_bc2f();
    uint32_t copbranch_flag_bc2t();
    uint32_t copbranch_bc0f();
    uint32_t copbranch_bc0t();
    uint32_t copbranch_bc0fl();
    uint32_t copbranch_bc0tl();
    uint32_t copbranch_bc1f();
    uint32_t copbranch_bc1t();
    uint32_t copbranch_bc1fl();
    uint32_t copbranch_bc1tl();
    uint32_t copbranch_bc2f();
    uint32_t copbranch_bc2t();
    uint32_t copbranch_bc2fl();
    uint32_t copbranch_bc2tl();
    uint32_t copbranch_bc3f();
    uint32_t copbranch_bc3t();
    uint32_t copbranch_bc3fl();
    uint32_t copbranch_bc3tl();

    // z: coprocessor number, f: branch-on-false encoding (rt bit 0 clear),
    // at: offset of the branch from the probe's entry.
    static const struct {
        const char * name;
        probe_t probe;
        uint32_t z, f, at;
    } s_probes[] = {
        { "bc0f",  copbranch_bc0f,  0, 1, 4 }, { "bc0t",  copbranch_bc0t,  0, 0, 4 },
        { "bc0fl", copbranch_bc0fl, 0, 1, 4 }, { "bc0tl", copbranch_bc0tl, 0, 0, 4 },
        { "bc1f",  copbranch_bc1f,  1, 1, 4 }, { "bc1t",  copbranch_bc1t,  1, 0, 4 },
        { "bc1fl", copbranch_bc1fl, 1, 1, 4 }, { "bc1tl", copbranch_bc1tl, 1, 0, 4 },
        { "bc2f",  copbranch_bc2f,  2, 1, 4 }, { "bc2t",  copbranch_bc2t,  2, 0, 4 },
        { "bc2fl", copbranch_bc2fl, 2, 1, 4 }, { "bc2tl", copbranch_bc2tl, 2, 0, 4 },
        { "bc3f",  copbranch_bc3f,  3, 1, 4 }, { "bc3t",  copbranch_bc3t,  3, 0, 4 },
        { "bc3fl", copbranch_bc3fl, 3, 1, 4 }, { "bc3tl", copbranch_bc3tl, 3, 0, 4 },
        { "rtpt_bc2f", copbranch_rtpt_bc2f, 2, 1, 12 }, { "rtpt_bc2t", copbranch_rtpt_bc2t, 2, 0, 12 },
        { "flag_bc2f", copbranch_flag_bc2f, 2, 1, 12 }, { "flag_bc2t", copbranch_flag_bc2t, 2, 0, 12 },
    };

    static uint32_t getSR() {
        uint32_t r;
        __asm__ volatile("mfc0 %0, $12\nnop" : "=r"(r));
        return r;
    }

    static void setSR(uint32_t r) {
        __asm__ volatile("mtc0 %0, $12\nnop\nnop" : : "r"(r));
    }

    // Runs one probe with SR.CU0-3 forced to cu, and reports
    // the path bits, plus whether an exception fired, its code,
    // the BD bit, and EPC relative to the probe's entry.
    static uint32_t runProbe(probe_t probe, uint32_t cu, uint32_t * exc, uint32_t * excode,
                             uint32_t * bd, int32_t * epcoff) {
        uint32_t sr = getSR();
        s_got80 = 0;
        s_cause = 0;
        s_epc = 0;
        setSR((sr & 0x0fffffff) | (cu << 28));
        uint32_t paths = probe();
        setSR(sr);
        *exc = s_got80;
        *excode = (s_cause >> 2) & 0x1f;
        *bd = s_cause >> 31;
        *epcoff = s_got80 ? (int32_t)(s_epc - (uint32_t)probe) : -1;
        return paths;
    }
)

CESTER_TEST(cop_branch_controls, cpu_tests,
    uint32_t exc, excode, bd;
    int32_t epcoff;
    uint32_t paths;

    paths = runProbe(copbranch_beq, 0x4, &exc, &excode, &bd, &epcoff);
    cester_assert_uint_eq(5, paths);
    cester_assert_uint_eq(0, exc);

    paths = runProbe(copbranch_bne, 0x4, &exc, &excode, &bd, &epcoff);
    cester_assert_uint_eq(3, paths);
    cester_assert_uint_eq(0, exc);

    paths = runProbe(copbranch_syscall, 0x4, &exc, &excode, &bd, &epcoff);
    cester_assert_uint_eq(3, paths);
    cester_assert_uint_eq(1, exc);
    cester_assert_uint_eq(8, excode);
    cester_assert_uint_eq(0, bd);
    cester_assert_int_eq(4, epcoff);
)

// With SR.CUz set, the coprocessor condition input reads as false for
// every coprocessor: BCzF always branches, BCzT never does, and rt bit 1
// (the MIPS II branch-likely forms) is ignored, so the delay slot always
// runs. With SR.CUz clear, the branch raises a coprocessor unusable
// exception with Cause.CE = z, including for COP0 in kernel mode.
CESTER_TEST(cop_branch_matrix, cpu_tests,
    static const uint32_t cus[] = { 0x0, 0x4, 0xf };
    for (unsigned c = 0; c < sizeof(cus) / sizeof(cus[0]); c++) {
        for (unsigned i = 0; i < sizeof(s_probes) / sizeof(s_probes[0]); i++) {
            uint32_t exc, excode, bd;
            int32_t epcoff;
            uint32_t paths = runProbe(s_probes[i].probe, cus[c], &exc, &excode, &bd, &epcoff);
            uint32_t ce = (s_cause >> 28) & 3;
            if (cus[c] & (1 << s_probes[i].z)) {
                cester_assert_uint_eq(s_probes[i].f ? 5 : 3, paths);
                cester_assert_uint_eq(0, exc);
            } else {
                cester_assert_uint_eq(3, paths);
                cester_assert_uint_eq(1, exc);
                cester_assert_uint_eq(11, excode);
                cester_assert_uint_eq(s_probes[i].z, ce);
                cester_assert_uint_eq(0, bd);
                cester_assert_int_eq(s_probes[i].at, epcoff);
            }
        }
    }
)

// COP0 register moves stay usable in kernel mode with CU0 clear, which
// the branches above do not.
CESTER_TEST(cop_branch_mfc0_kernel, cpu_tests,
    uint32_t exc, excode, bd;
    int32_t epcoff;
    uint32_t paths = runProbe(copbranch_mfc0, 0x0, &exc, &excode, &bd, &epcoff);
    cester_assert_uint_eq(3, paths);
    cester_assert_uint_eq(0, exc);
)
