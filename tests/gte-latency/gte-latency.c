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

// GTE input-register latency probe - lighting family.
// Covers NCCT (this binary's primary focus); the other lighting variants
// (NCS, NCT, NCCS, NCDS, NCDT) are added as additional CESTER_TEST blocks.
//
// See gte-latency-common.h for the probe macro details.

#include "gte-latency-common.h"

// clang-format off

// Cester re-includes this file via __BASE_FILE__ to register tests.
// Static helpers must be guarded so they aren't redefined on the second pass.
#ifndef GTE_LATENCY_LIGHTING_HELPERS_DEFINED
#define GTE_LATENCY_LIGHTING_HELPERS_DEFINED

// Test scene used by all the lighting-family tests.
//
// LLM, LCM = identity diagonal (1.0 = 0x1000 in the 12.12 fixed-point
// the GTE uses).
//
// V0 = (0, 0, 0x1000)   normal facing the +Z light
// V1 = (0x1000, 0, 0)   normal along +X
// V2 = (0, 0x1000, 0)   normal along +Y
// BK = FC = 0
// RGBC = (R=0x80, G=0x80, B=0x80, CODE=0)
// IR0 = 0x1000          (used by INTPL/DCPL but kept stable across tests)
// IR1/2/3 baseline matches V0's lighting result
//
// We reset every input the lighting tests use, before every probe iteration,
// so prior canary writes can never leak between probes or tests.
static inline void scene_setup(void) {
    // LLM = identity diagonal
    cop2_putc(8,  0x00001000);
    cop2_putc(9,  0x00000000);
    cop2_putc(10, 0x00001000);
    cop2_putc(11, 0x00000000);
    cop2_putc(12, 0x1000);
    // LCM = identity diagonal
    cop2_putc(16, 0x00001000);
    cop2_putc(17, 0x00000000);
    cop2_putc(18, 0x00001000);
    cop2_putc(19, 0x00000000);
    cop2_putc(20, 0x1000);
    cop2_putc(13, 0); cop2_putc(14, 0); cop2_putc(15, 0);   // BK
    cop2_putc(21, 0); cop2_putc(22, 0); cop2_putc(23, 0);   // FC
    cop2_put(0, 0x00000000);                  // V0
    cop2_put(1, 0x00001000);
    cop2_put(2, (0x0000u << 16) | 0x1000u);   // V1
    cop2_put(3, 0);
    cop2_put(4, (0x1000u << 16) | 0x0000u);   // V2
    cop2_put(5, 0);
    cop2_put(6, 0x00808080);                  // RGBC
    cop2_put(8, 0x1000);                      // IR0 (12-bit unsigned, 1.0)
    cop2_putc(31, 0);                         // FLAG
}

// Compile-time GTE op encodings. The COP2_X(sf, lm) macros expand to
// integer constants, which is what the inline asm "i" constraint needs.
#define OP_NCCT_SF1_LM1   COP2_NCCT(1, 1)
#define OP_NCCS_SF1_LM1   COP2_NCCS(1, 1)
#define OP_NCDT_SF1_LM1   COP2_NCDT(1, 1)
#define OP_NCDS_SF1_LM1   COP2_NCDS(1, 1)
#define OP_NCT_SF1_LM1    COP2_NCT(1, 1)
#define OP_NCS_SF1_LM1    COP2_NCS(1, 1)

// Canary values: chosen to be wildly different from baseline and to
// produce clearly different outputs.
#define CANARY_VXY  0x40004000u   // VX=0x4000, VY=0x4000
#define CANARY_VZ   0x00004000u   // VZ=0x4000
#define CANARY_RGBC 0x00404040u   // R=G=B=0x40 (half intensity)
#define CANARY_LMAT 0x00000800u   // 0.5 in matrix entry (vs identity 1.0=0x1000)
#define CANARY_BK   0x00010000u   // BK = 65536 (16.0)
#define CANARY_FC   0x00010000u   // FC = 65536

#endif // GTE_LATENCY_LIGHTING_HELPERS_DEFINED

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

CESTER_BEFORE_ALL(gte_latency_tests,
    gte_enable();
)

// ==========================================================================
// Smoke test: verify the test scene gives the math we expect.
// ==========================================================================
CESTER_TEST(ncct_scene_smoke, gte_latency_tests,
    scene_setup();
    cop2_cmd(COP2_NCCT(1, 1));
    probe_result_t r;
    read_full_state(&r);
    ramsyscall_printf("smoke NCCT: RGB=(0x%08x,0x%08x,0x%08x) MAC=(%d,%d,%d) FLAG=0x%08x\n",
                      r.rgb0, r.rgb1, r.rgb2, r.mac1, r.mac2, r.mac3, r.flag);
    cester_assert_uint_eq(0x00800000, r.rgb0);
    cester_assert_uint_eq(0x00000080, r.rgb1);
    cester_assert_uint_eq(0x00008000, r.rgb2);
    cester_assert_int_eq(0,     r.mac1);
    cester_assert_int_eq(0x800, r.mac2);
    cester_assert_int_eq(0,     r.mac3);
)

// ==========================================================================
// NCCT: vertex registers (data 0..5)
// ==========================================================================
MAKE_DATA_TEST(ncct_vxy0, scene_setup, OP_NCCT_SF1_LM1, 0, CANARY_VXY,  "NCCT VXY0")
MAKE_DATA_TEST(ncct_vz0,  scene_setup, OP_NCCT_SF1_LM1, 1, CANARY_VZ,   "NCCT VZ0")
MAKE_DATA_TEST(ncct_vxy1, scene_setup, OP_NCCT_SF1_LM1, 2, CANARY_VXY,  "NCCT VXY1")
MAKE_DATA_TEST(ncct_vz1,  scene_setup, OP_NCCT_SF1_LM1, 3, CANARY_VZ,   "NCCT VZ1")
MAKE_DATA_TEST(ncct_vxy2, scene_setup, OP_NCCT_SF1_LM1, 4, CANARY_VXY,  "NCCT VXY2")
MAKE_DATA_TEST(ncct_vz2,  scene_setup, OP_NCCT_SF1_LM1, 5, CANARY_VZ,   "NCCT VZ2")
MAKE_DATA_TEST(ncct_rgbc, scene_setup, OP_NCCT_SF1_LM1, 6, CANARY_RGBC, "NCCT RGBC")

// ==========================================================================
// NCCT: light matrix (control 8..12) - the "input" rows that NCCT reads
// ==========================================================================
MAKE_CTRL_TEST(ncct_l11l12, scene_setup, OP_NCCT_SF1_LM1,  8, CANARY_LMAT, "NCCT L11L12")
MAKE_CTRL_TEST(ncct_l13l21, scene_setup, OP_NCCT_SF1_LM1,  9, CANARY_LMAT, "NCCT L13L21")
MAKE_CTRL_TEST(ncct_l22l23, scene_setup, OP_NCCT_SF1_LM1, 10, CANARY_LMAT, "NCCT L22L23")
MAKE_CTRL_TEST(ncct_l31l32, scene_setup, OP_NCCT_SF1_LM1, 11, CANARY_LMAT, "NCCT L31L32")
MAKE_CTRL_TEST(ncct_l33,    scene_setup, OP_NCCT_SF1_LM1, 12, CANARY_LMAT, "NCCT L33")

// ==========================================================================
// NCCT: light color matrix (control 16..20)
// ==========================================================================
MAKE_CTRL_TEST(ncct_lr1lr2, scene_setup, OP_NCCT_SF1_LM1, 16, CANARY_LMAT, "NCCT LR1LR2")
MAKE_CTRL_TEST(ncct_lr3lg1, scene_setup, OP_NCCT_SF1_LM1, 17, CANARY_LMAT, "NCCT LR3LG1")
MAKE_CTRL_TEST(ncct_lg2lg3, scene_setup, OP_NCCT_SF1_LM1, 18, CANARY_LMAT, "NCCT LG2LG3")
MAKE_CTRL_TEST(ncct_lb1lb2, scene_setup, OP_NCCT_SF1_LM1, 19, CANARY_LMAT, "NCCT LB1LB2")
MAKE_CTRL_TEST(ncct_lb3,    scene_setup, OP_NCCT_SF1_LM1, 20, CANARY_LMAT, "NCCT LB3")

// ==========================================================================
// NCCT: background color (control 13..15)
// ==========================================================================
MAKE_CTRL_TEST(ncct_rbk, scene_setup, OP_NCCT_SF1_LM1, 13, CANARY_BK, "NCCT RBK")
MAKE_CTRL_TEST(ncct_gbk, scene_setup, OP_NCCT_SF1_LM1, 14, CANARY_BK, "NCCT GBK")
MAKE_CTRL_TEST(ncct_bbk, scene_setup, OP_NCCT_SF1_LM1, 15, CANARY_BK, "NCCT BBK")
