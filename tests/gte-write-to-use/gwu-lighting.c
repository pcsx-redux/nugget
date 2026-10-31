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

// Generated from tests/gte-latency/gte-latency.c: same scenes, canaries and cells.

#include "gwu.h"

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
// VZ canary chosen smaller than baseline (0x1000) so the perturbed
// pipeline doesn't saturate to the same RGB value as the baseline.
// 0x4000 was too large for NCT (no per-vertex color tint to shrink the
// pipeline output), causing the sanity-pre output to saturate to the
// same 0xff as baseline.
#define CANARY_VZ   0x00000100u
#define CANARY_RGBC 0x00404040u   // R=G=B=0x40 (half intensity)
#define CANARY_LMAT 0x00000800u   // 0.5 in matrix entry (vs identity 1.0=0x1000)
#define CANARY_BK   0x00010000u   // BK = 65536 (16.0)
#define CANARY_FC   0x00010000u   // FC = 65536

#endif // GTE_LATENCY_LIGHTING_HELPERS_DEFINED


#ifndef NCDT_HELPERS_DEFINED
#define NCDT_HELPERS_DEFINED
static inline void scene_setup_ncdt(void) {
    scene_setup();
    cop2_putc(21, 0x1000);   // RFC = 0x1000
    cop2_putc(22, 0x1000);   // GFC
    cop2_putc(23, 0x1000);   // BFC
    cop2_put(8,  0x0800);    // IR0 = 0.5 in 1.12
}
#endif

#define CELLS(X) \
    X(D, ncct_vxy0, scene_setup, OP_NCCT_SF1_LM1, 0, CANARY_VXY, "NCCT VXY0", "XXNN") \
    X(D, ncct_vz0, scene_setup, OP_NCCT_SF1_LM1, 1, CANARY_VZ, "NCCT VZ0", "NNNN") \
    X(D, ncct_vxy1, scene_setup, OP_NCCT_SF1_LM1, 2, CANARY_VXY, "NCCT VXY1", "NNNN") \
    X(D, ncct_vz1, scene_setup, OP_NCCT_SF1_LM1, 3, CANARY_VZ, "NCCT VZ1", "NNNN") \
    X(D, ncct_vxy2, scene_setup, OP_NCCT_SF1_LM1, 4, CANARY_VXY, "NCCT VXY2", "NNNN") \
    X(D, ncct_vz2, scene_setup, OP_NCCT_SF1_LM1, 5, CANARY_VZ, "NCCT VZ2", "NNNN") \
    X(D, ncct_rgbc, scene_setup, OP_NCCT_SF1_LM1, 6, CANARY_RGBC, "NCCT RGBC", "NNNN") \
    X(C, ncct_l11l12, scene_setup, OP_NCCT_SF1_LM1, 8, CANARY_LMAT, "NCCT L11L12", "NNNN") \
    X(C, ncct_l13l21, scene_setup, OP_NCCT_SF1_LM1, 9, CANARY_LMAT, "NCCT L13L21", "NNNN") \
    X(C, ncct_l22l23, scene_setup, OP_NCCT_SF1_LM1, 10, CANARY_LMAT, "NCCT L22L23", "NNNN") \
    X(C, ncct_l31l32, scene_setup, OP_NCCT_SF1_LM1, 11, CANARY_LMAT, "NCCT L31L32", "NNNN") \
    X(C, ncct_l33, scene_setup, OP_NCCT_SF1_LM1, 12, CANARY_LMAT, "NCCT L33", "NNNN") \
    X(C, ncct_lr1lr2, scene_setup, OP_NCCT_SF1_LM1, 16, CANARY_LMAT, "NCCT LR1LR2", "NNNN") \
    X(C, ncct_lr3lg1, scene_setup, OP_NCCT_SF1_LM1, 17, CANARY_LMAT, "NCCT LR3LG1", "NNNN") \
    X(C, ncct_lg2lg3, scene_setup, OP_NCCT_SF1_LM1, 18, CANARY_LMAT, "NCCT LG2LG3", "NNNN") \
    X(C, ncct_lb1lb2, scene_setup, OP_NCCT_SF1_LM1, 19, CANARY_LMAT, "NCCT LB1LB2", "NNNN") \
    X(C, ncct_lb3, scene_setup, OP_NCCT_SF1_LM1, 20, CANARY_LMAT, "NCCT LB3", "NNNN") \
    X(C, ncct_rbk, scene_setup, OP_NCCT_SF1_LM1, 13, CANARY_BK, "NCCT RBK", "NNNN") \
    X(C, ncct_gbk, scene_setup, OP_NCCT_SF1_LM1, 14, CANARY_BK, "NCCT GBK", "NNNN") \
    X(C, ncct_bbk, scene_setup, OP_NCCT_SF1_LM1, 15, CANARY_BK, "NCCT BBK", "NNNN") \
    X(D, nct_vxy0, scene_setup, OP_NCT_SF1_LM1, 0, CANARY_VXY, "NCT VXY0", "XXNN") \
    X(D, nct_vz0, scene_setup, OP_NCT_SF1_LM1, 1, CANARY_VZ, "NCT VZ0", "NNNN") \
    X(D, nct_vxy1, scene_setup, OP_NCT_SF1_LM1, 2, CANARY_VXY, "NCT VXY1", "NNNN") \
    X(D, nct_vz1, scene_setup, OP_NCT_SF1_LM1, 3, CANARY_VZ, "NCT VZ1", "NNNN") \
    X(D, nct_vxy2, scene_setup, OP_NCT_SF1_LM1, 4, CANARY_VXY, "NCT VXY2", "NNNN") \
    X(D, nct_vz2, scene_setup, OP_NCT_SF1_LM1, 5, CANARY_VZ, "NCT VZ2", "NNNN") \
    X(C, nct_l11l12, scene_setup, OP_NCT_SF1_LM1, 8, CANARY_LMAT, "NCT L11L12", "NNNN") \
    X(C, nct_l13l21, scene_setup, OP_NCT_SF1_LM1, 9, CANARY_LMAT, "NCT L13L21", "NNNN") \
    X(C, nct_l22l23, scene_setup, OP_NCT_SF1_LM1, 10, CANARY_LMAT, "NCT L22L23", "NNNN") \
    X(C, nct_l31l32, scene_setup, OP_NCT_SF1_LM1, 11, CANARY_LMAT, "NCT L31L32", "NNNN") \
    X(C, nct_l33, scene_setup, OP_NCT_SF1_LM1, 12, CANARY_LMAT, "NCT L33", "NNNN") \
    X(C, nct_lr1lr2, scene_setup, OP_NCT_SF1_LM1, 16, CANARY_LMAT, "NCT LR1LR2", "NNNN") \
    X(C, nct_lr3lg1, scene_setup, OP_NCT_SF1_LM1, 17, CANARY_LMAT, "NCT LR3LG1", "NNNN") \
    X(C, nct_lg2lg3, scene_setup, OP_NCT_SF1_LM1, 18, CANARY_LMAT, "NCT LG2LG3", "NNNN") \
    X(C, nct_lb1lb2, scene_setup, OP_NCT_SF1_LM1, 19, CANARY_LMAT, "NCT LB1LB2", "NNNN") \
    X(C, nct_lb3, scene_setup, OP_NCT_SF1_LM1, 20, CANARY_LMAT, "NCT LB3", "NNNN") \
    X(C, nct_rbk, scene_setup, OP_NCT_SF1_LM1, 13, CANARY_BK, "NCT RBK", "NNNN") \
    X(C, nct_gbk, scene_setup, OP_NCT_SF1_LM1, 14, CANARY_BK, "NCT GBK", "NNNN") \
    X(C, nct_bbk, scene_setup, OP_NCT_SF1_LM1, 15, CANARY_BK, "NCT BBK", "NNNN") \
    X(D, ncdt_vxy0, scene_setup_ncdt, OP_NCDT_SF1_LM1, 0, CANARY_VXY, "NCDT VXY0", "XXNN") \
    X(D, ncdt_vz0, scene_setup_ncdt, OP_NCDT_SF1_LM1, 1, CANARY_VZ, "NCDT VZ0", "NNNN") \
    X(D, ncdt_vxy1, scene_setup_ncdt, OP_NCDT_SF1_LM1, 2, CANARY_VXY, "NCDT VXY1", "NNNN") \
    X(D, ncdt_vz1, scene_setup_ncdt, OP_NCDT_SF1_LM1, 3, CANARY_VZ, "NCDT VZ1", "NNNN") \
    X(D, ncdt_vxy2, scene_setup_ncdt, OP_NCDT_SF1_LM1, 4, CANARY_VXY, "NCDT VXY2", "NNNN") \
    X(D, ncdt_vz2, scene_setup_ncdt, OP_NCDT_SF1_LM1, 5, CANARY_VZ, "NCDT VZ2", "NNNN") \
    X(D, ncdt_rgbc, scene_setup_ncdt, OP_NCDT_SF1_LM1, 6, CANARY_RGBC, "NCDT RGBC", "NNNN") \
    X(C, ncdt_l11l12, scene_setup_ncdt, OP_NCDT_SF1_LM1, 8, CANARY_LMAT, "NCDT L11L12", "NNNN") \
    X(C, ncdt_l13l21, scene_setup_ncdt, OP_NCDT_SF1_LM1, 9, CANARY_LMAT, "NCDT L13L21", "NNNN") \
    X(C, ncdt_l22l23, scene_setup_ncdt, OP_NCDT_SF1_LM1, 10, CANARY_LMAT, "NCDT L22L23", "NNNN") \
    X(C, ncdt_l31l32, scene_setup_ncdt, OP_NCDT_SF1_LM1, 11, CANARY_LMAT, "NCDT L31L32", "NNNN") \
    X(C, ncdt_l33, scene_setup_ncdt, OP_NCDT_SF1_LM1, 12, CANARY_LMAT, "NCDT L33", "NNNN") \
    X(C, ncdt_lr1lr2, scene_setup_ncdt, OP_NCDT_SF1_LM1, 16, CANARY_LMAT, "NCDT LR1LR2", "NNNN") \
    X(C, ncdt_lr3lg1, scene_setup_ncdt, OP_NCDT_SF1_LM1, 17, CANARY_LMAT, "NCDT LR3LG1", "NNNN") \
    X(C, ncdt_lg2lg3, scene_setup_ncdt, OP_NCDT_SF1_LM1, 18, CANARY_LMAT, "NCDT LG2LG3", "NNNN") \
    X(C, ncdt_lb1lb2, scene_setup_ncdt, OP_NCDT_SF1_LM1, 19, CANARY_LMAT, "NCDT LB1LB2", "NNNN") \
    X(C, ncdt_lb3, scene_setup_ncdt, OP_NCDT_SF1_LM1, 20, CANARY_LMAT, "NCDT LB3", "NNNN") \
    X(C, ncdt_rbk, scene_setup_ncdt, OP_NCDT_SF1_LM1, 13, CANARY_BK, "NCDT RBK", "NNNN") \
    X(C, ncdt_gbk, scene_setup_ncdt, OP_NCDT_SF1_LM1, 14, CANARY_BK, "NCDT GBK", "NNNN") \
    X(C, ncdt_bbk, scene_setup_ncdt, OP_NCDT_SF1_LM1, 15, CANARY_BK, "NCDT BBK", "NNNN") \
    X(C, ncdt_rfc, scene_setup_ncdt, OP_NCDT_SF1_LM1, 21, CANARY_FC, "NCDT RFC", "NNNN") \
    X(C, ncdt_gfc, scene_setup_ncdt, OP_NCDT_SF1_LM1, 22, CANARY_FC, "NCDT GFC", "NNNN") \
    X(C, ncdt_bfc, scene_setup_ncdt, OP_NCDT_SF1_LM1, 23, CANARY_FC, "NCDT BFC", "NNNN") \

CELLS(GWU_FUNCS)

const GwuCell gwu_cells_lighting[] = {CELLS(GWU_ENTRY)};
const int gwu_count_lighting = sizeof(gwu_cells_lighting) / sizeof(gwu_cells_lighting[0]);
