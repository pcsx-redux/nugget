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

// Generated from tests/gte-latency-singles/gte-latency-singles.c: same scenes, canaries and cells.

#include "gwu.h"


// clang-format off

#ifndef GTE_LATENCY_SINGLES_HELPERS_DEFINED
#define GTE_LATENCY_SINGLES_HELPERS_DEFINED

// Test scene shared by all the single-vertex lighting tests.
//
// V0 = (0x600, 0x800, 0xA00)   non-axis-aligned so EVERY L matrix entry
//                              contributes to the lighting result. With an
//                              axis-aligned V0, the canary on, say, L11
//                              would have no effect because L11 is always
//                              multiplied by V0_X=0 - we'd report a false
//                              "latched at N=0" boundary.
// LLM = identity diagonal
// LCM = identity diagonal
// BK = (0, 0, 0)
// FC = (0x1000, 0x1000, 0x1000)   used by NCDS
// IR0 = 0x800                     used by NCDS depth cue
// RGBC = (0x80, 0x80, 0x80, 0)   used by NCCS / NCDS color stage
static inline void scene_setup(void) {
    cop2_putc(8,  0x00001000);
    cop2_putc(9,  0x00000000);
    cop2_putc(10, 0x00001000);
    cop2_putc(11, 0x00000000);
    cop2_putc(12, 0x1000);
    cop2_putc(16, 0x00001000);
    cop2_putc(17, 0x00000000);
    cop2_putc(18, 0x00001000);
    cop2_putc(19, 0x00000000);
    cop2_putc(20, 0x1000);
    cop2_putc(13, 0); cop2_putc(14, 0); cop2_putc(15, 0);   // BK
    cop2_putc(21, 0x1000); cop2_putc(22, 0x1000); cop2_putc(23, 0x1000);   // FC
    cop2_put(0, (0x0800u << 16) | 0x0600u);  // V0: VX=0x600, VY=0x800
    cop2_put(1, 0x00000A00);                 //     VZ=0xA00
    // V1, V2 set to placeholders (NCS/NCCS/NCDS don't read them).
    cop2_put(2, 0); cop2_put(3, 0);
    cop2_put(4, 0); cop2_put(5, 0);
    cop2_put(6, 0x00808080);   // RGBC
    cop2_put(8, 0x0800);       // IR0 = 0.5
    // Clear RGB FIFO and SXY FIFO so stale entries from prior probes
    // don't end up in the comparison. Single-vertex lighting ops only
    // push one entry per call; the older slots would otherwise hold
    // garbage from earlier tests.
    cop2_put(20, 0); cop2_put(21, 0); cop2_put(22, 0);   // RGB FIFO
    cop2_put(12, 0); cop2_put(13, 0); cop2_put(14, 0);   // SXY FIFO entries
    cop2_put(16, 0); cop2_put(17, 0); cop2_put(18, 0); cop2_put(19, 0);  // SZ FIFO
    cop2_put(24, 0); cop2_put(25, 0); cop2_put(26, 0); cop2_put(27, 0);  // MAC0..3
    cop2_putc(31, 0);          // FLAG
}

#define OP_NCS_SF1_LM1   COP2_NCS(1, 1)
#define OP_NCCS_SF1_LM1  COP2_NCCS(1, 1)
#define OP_NCDS_SF1_LM1  COP2_NCDS(1, 1)

// Canaries: values different from baseline that produce visibly
// different GTE outputs. For 16/16-packed matrix entries we set BOTH
// halves so the canary perturbs both the low and high entries.
#define CANARY_VXY  0x05000400u   // VX=0x400, VY=0x500 (vs 0x600/0x800)
#define CANARY_VZ   0x00000400u   // VZ=0x400 (vs 0xA00)
#define CANARY_RGBC 0x00404040u
#define CANARY_LMAT 0x07ff07ffu   // both halves 0x7ff (off-diagonal 0 -> 0x7ff,
                                  // diagonal 0x1000 -> 0x7ff)
#define CANARY_BK   0x00010000u
#define CANARY_FC   0x00010000u

#endif // GTE_LATENCY_SINGLES_HELPERS_DEFINED


#define CELLS(X) \
    X(D, ncs_vxy0, scene_setup, OP_NCS_SF1_LM1, 0, CANARY_VXY, "NCS VXY0", "XXNN") \
    X(D, ncs_vz0, scene_setup, OP_NCS_SF1_LM1, 1, CANARY_VZ, "NCS VZ0", "NNNN") \
    X(C, ncs_l11l12, scene_setup, OP_NCS_SF1_LM1, 8, CANARY_LMAT, "NCS L11L12", "OXNN") \
    X(C, ncs_l13l21, scene_setup, OP_NCS_SF1_LM1, 9, CANARY_LMAT, "NCS L13L21", "XNNN") \
    X(C, ncs_l22l23, scene_setup, OP_NCS_SF1_LM1, 10, CANARY_LMAT, "NCS L22L23", "NNNN") \
    X(C, ncs_l31l32, scene_setup, OP_NCS_SF1_LM1, 11, CANARY_LMAT, "NCS L31L32", "NNNN") \
    X(C, ncs_l33, scene_setup, OP_NCS_SF1_LM1, 12, CANARY_LMAT, "NCS L33", "NNNN") \
    X(C, ncs_lr1lr2, scene_setup, OP_NCS_SF1_LM1, 16, CANARY_LMAT, "NCS LR1LR2", "NNNN") \
    X(C, ncs_lr3lg1, scene_setup, OP_NCS_SF1_LM1, 17, CANARY_LMAT, "NCS LR3LG1", "NNNN") \
    X(C, ncs_lg2lg3, scene_setup, OP_NCS_SF1_LM1, 18, CANARY_LMAT, "NCS LG2LG3", "NNNN") \
    X(C, ncs_lb1lb2, scene_setup, OP_NCS_SF1_LM1, 19, CANARY_LMAT, "NCS LB1LB2", "NNNN") \
    X(C, ncs_lb3, scene_setup, OP_NCS_SF1_LM1, 20, CANARY_LMAT, "NCS LB3", "NNNN") \
    X(C, ncs_rbk, scene_setup, OP_NCS_SF1_LM1, 13, CANARY_BK, "NCS RBK", "NNNN") \
    X(C, ncs_gbk, scene_setup, OP_NCS_SF1_LM1, 14, CANARY_BK, "NCS GBK", "NNNN") \
    X(C, ncs_bbk, scene_setup, OP_NCS_SF1_LM1, 15, CANARY_BK, "NCS BBK", "NNNN") \
    X(D, nccs_vxy0, scene_setup, OP_NCCS_SF1_LM1, 0, CANARY_VXY, "NCCS VXY0", "XXNN") \
    X(D, nccs_vz0, scene_setup, OP_NCCS_SF1_LM1, 1, CANARY_VZ, "NCCS VZ0", "NNNN") \
    X(D, nccs_rgbc, scene_setup, OP_NCCS_SF1_LM1, 6, CANARY_RGBC, "NCCS RGBC", "NNNN") \
    X(C, nccs_l11l12, scene_setup, OP_NCCS_SF1_LM1, 8, CANARY_LMAT, "NCCS L11L12", "ONNN") \
    X(C, nccs_l13l21, scene_setup, OP_NCCS_SF1_LM1, 9, CANARY_LMAT, "NCCS L13L21", "XNNN") \
    X(C, nccs_l22l23, scene_setup, OP_NCCS_SF1_LM1, 10, CANARY_LMAT, "NCCS L22L23", "NNNN") \
    X(C, nccs_l31l32, scene_setup, OP_NCCS_SF1_LM1, 11, CANARY_LMAT, "NCCS L31L32", "NNNN") \
    X(C, nccs_l33, scene_setup, OP_NCCS_SF1_LM1, 12, CANARY_LMAT, "NCCS L33", "NNNN") \
    X(C, nccs_lr1lr2, scene_setup, OP_NCCS_SF1_LM1, 16, CANARY_LMAT, "NCCS LR1LR2", "NNNN") \
    X(C, nccs_lr3lg1, scene_setup, OP_NCCS_SF1_LM1, 17, CANARY_LMAT, "NCCS LR3LG1", "NNNN") \
    X(C, nccs_lg2lg3, scene_setup, OP_NCCS_SF1_LM1, 18, CANARY_LMAT, "NCCS LG2LG3", "NNNN") \
    X(C, nccs_lb1lb2, scene_setup, OP_NCCS_SF1_LM1, 19, CANARY_LMAT, "NCCS LB1LB2", "NNNN") \
    X(C, nccs_lb3, scene_setup, OP_NCCS_SF1_LM1, 20, CANARY_LMAT, "NCCS LB3", "NNNN") \
    X(C, nccs_rbk, scene_setup, OP_NCCS_SF1_LM1, 13, CANARY_BK, "NCCS RBK", "NNNN") \
    X(C, nccs_gbk, scene_setup, OP_NCCS_SF1_LM1, 14, CANARY_BK, "NCCS GBK", "NNNN") \
    X(C, nccs_bbk, scene_setup, OP_NCCS_SF1_LM1, 15, CANARY_BK, "NCCS BBK", "NNNN") \
    X(D, ncds_vxy0, scene_setup, OP_NCDS_SF1_LM1, 0, CANARY_VXY, "NCDS VXY0", "XXNN") \
    X(D, ncds_vz0, scene_setup, OP_NCDS_SF1_LM1, 1, CANARY_VZ, "NCDS VZ0", "NNNN") \
    X(D, ncds_rgbc, scene_setup, OP_NCDS_SF1_LM1, 6, CANARY_RGBC, "NCDS RGBC", "NNNN") \
    X(C, ncds_l11l12, scene_setup, OP_NCDS_SF1_LM1, 8, CANARY_LMAT, "NCDS L11L12", "OXNN") \
    X(C, ncds_l13l21, scene_setup, OP_NCDS_SF1_LM1, 9, CANARY_LMAT, "NCDS L13L21", "XNNN") \
    X(C, ncds_l22l23, scene_setup, OP_NCDS_SF1_LM1, 10, CANARY_LMAT, "NCDS L22L23", "NNNN") \
    X(C, ncds_l31l32, scene_setup, OP_NCDS_SF1_LM1, 11, CANARY_LMAT, "NCDS L31L32", "NNNN") \
    X(C, ncds_l33, scene_setup, OP_NCDS_SF1_LM1, 12, CANARY_LMAT, "NCDS L33", "NNNN") \
    X(C, ncds_lr1lr2, scene_setup, OP_NCDS_SF1_LM1, 16, CANARY_LMAT, "NCDS LR1LR2", "NNNN") \
    X(C, ncds_lr3lg1, scene_setup, OP_NCDS_SF1_LM1, 17, CANARY_LMAT, "NCDS LR3LG1", "NNNN") \
    X(C, ncds_lg2lg3, scene_setup, OP_NCDS_SF1_LM1, 18, CANARY_LMAT, "NCDS LG2LG3", "NNNN") \
    X(C, ncds_lb1lb2, scene_setup, OP_NCDS_SF1_LM1, 19, CANARY_LMAT, "NCDS LB1LB2", "NNNN") \
    X(C, ncds_lb3, scene_setup, OP_NCDS_SF1_LM1, 20, CANARY_LMAT, "NCDS LB3", "NNNN") \
    X(C, ncds_rbk, scene_setup, OP_NCDS_SF1_LM1, 13, CANARY_BK, "NCDS RBK", "NNNN") \
    X(C, ncds_gbk, scene_setup, OP_NCDS_SF1_LM1, 14, CANARY_BK, "NCDS GBK", "NNNN") \
    X(C, ncds_bbk, scene_setup, OP_NCDS_SF1_LM1, 15, CANARY_BK, "NCDS BBK", "NNNN") \
    X(C, ncds_rfc, scene_setup, OP_NCDS_SF1_LM1, 21, CANARY_FC, "NCDS RFC", "NNNN") \
    X(C, ncds_gfc, scene_setup, OP_NCDS_SF1_LM1, 22, CANARY_FC, "NCDS GFC", "NNNN") \
    X(C, ncds_bfc, scene_setup, OP_NCDS_SF1_LM1, 23, CANARY_FC, "NCDS BFC", "NNNN") \

CELLS(GWU_FUNCS)

const GwuCell gwu_cells_singles[] = {CELLS(GWU_ENTRY)};
const int gwu_count_singles = sizeof(gwu_cells_singles) / sizeof(gwu_cells_singles[0]);
