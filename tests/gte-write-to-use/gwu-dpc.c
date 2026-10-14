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

// Generated from tests/gte-latency-dpc/gte-latency-dpc.c: same scenes, canaries and cells.

#include "gwu.h"


// clang-format off

#ifndef GTE_LATENCY_DPC_HELPERS_DEFINED
#define GTE_LATENCY_DPC_HELPERS_DEFINED

// Shared scene for all DPC-family tests.
// DPCS / DCPL / INTPL: read RGBC, FC, IR0, IR1/2/3.
// DPCT: reads RGB FIFO (RGB0/1/2), FC, IR0.
static inline void scene_setup(void) {
    cop2_put(6,  0x00808080);   // RGBC
    cop2_put(8,  0x0800);       // IR0 = 0.5
    cop2_put(9,  0x0500);       // IR1
    cop2_put(10, 0x0600);       // IR2
    cop2_put(11, 0x0700);       // IR3

    // RGB FIFO entries for DPCT
    cop2_put(20, 0x00808080);   // RGB0
    cop2_put(21, 0x00404040);   // RGB1
    cop2_put(22, 0x00202020);   // RGB2

    // FC
    cop2_putc(21, 0x1000); cop2_putc(22, 0x1000); cop2_putc(23, 0x1000);

    // Other state
    cop2_put(12, 0); cop2_put(13, 0); cop2_put(14, 0);   // SXY FIFO
    cop2_put(16, 0); cop2_put(17, 0); cop2_put(18, 0); cop2_put(19, 0);  // SZ FIFO
    cop2_put(24, 0); cop2_put(25, 0); cop2_put(26, 0); cop2_put(27, 0);

    cop2_putc(31, 0);
}

#define OP_DPCS_SF1_LM1  COP2_DPCS(1, 1)
#define OP_DPCT_SF1_LM1  COP2_DPCT(1, 1)
#define OP_DCPL_SF1_LM1  COP2_DCPL(1, 1)
#define OP_INTPL_SF1_LM1 COP2_INTPL(1, 1)

#define CANARY_RGBC 0x00404040u
#define CANARY_IR0  0x0400u   // smaller than baseline 0x800
#define CANARY_IR   0x0123u
#define CANARY_FC   0x00010000u
#define CANARY_RGBFIFO 0x00f0f0f0u

#endif // GTE_LATENCY_DPC_HELPERS_DEFINED


#define CELLS(X) \
    X(D, dpcs_rgbc, scene_setup, OP_DPCS_SF1_LM1, 6, CANARY_RGBC, "DPCS RGBC") \
    X(D, dpcs_ir0, scene_setup, OP_DPCS_SF1_LM1, 8, CANARY_IR0, "DPCS IR0") \
    X(C, dpcs_rfc, scene_setup, OP_DPCS_SF1_LM1, 21, CANARY_FC, "DPCS RFC") \
    X(C, dpcs_gfc, scene_setup, OP_DPCS_SF1_LM1, 22, CANARY_FC, "DPCS GFC") \
    X(C, dpcs_bfc, scene_setup, OP_DPCS_SF1_LM1, 23, CANARY_FC, "DPCS BFC") \
    X(D, dpct_rgb0, scene_setup, OP_DPCT_SF1_LM1, 20, CANARY_RGBFIFO, "DPCT RGB0") \
    X(D, dpct_rgb1, scene_setup, OP_DPCT_SF1_LM1, 21, CANARY_RGBFIFO, "DPCT RGB1") \
    X(D, dpct_rgb2, scene_setup, OP_DPCT_SF1_LM1, 22, CANARY_RGBFIFO, "DPCT RGB2") \
    X(D, dpct_ir0, scene_setup, OP_DPCT_SF1_LM1, 8, CANARY_IR0, "DPCT IR0") \
    X(C, dpct_rfc, scene_setup, OP_DPCT_SF1_LM1, 21, CANARY_FC, "DPCT RFC") \
    X(C, dpct_gfc, scene_setup, OP_DPCT_SF1_LM1, 22, CANARY_FC, "DPCT GFC") \
    X(C, dpct_bfc, scene_setup, OP_DPCT_SF1_LM1, 23, CANARY_FC, "DPCT BFC") \
    X(D, dcpl_rgbc, scene_setup, OP_DCPL_SF1_LM1, 6, CANARY_RGBC, "DCPL RGBC") \
    X(D, dcpl_ir1, scene_setup, OP_DCPL_SF1_LM1, 9, CANARY_IR, "DCPL IR1") \
    X(D, dcpl_ir2, scene_setup, OP_DCPL_SF1_LM1, 10, CANARY_IR, "DCPL IR2") \
    X(D, dcpl_ir3, scene_setup, OP_DCPL_SF1_LM1, 11, CANARY_IR, "DCPL IR3") \
    X(D, dcpl_ir0, scene_setup, OP_DCPL_SF1_LM1, 8, CANARY_IR0, "DCPL IR0") \
    X(C, dcpl_rfc, scene_setup, OP_DCPL_SF1_LM1, 21, CANARY_FC, "DCPL RFC") \
    X(C, dcpl_gfc, scene_setup, OP_DCPL_SF1_LM1, 22, CANARY_FC, "DCPL GFC") \
    X(C, dcpl_bfc, scene_setup, OP_DCPL_SF1_LM1, 23, CANARY_FC, "DCPL BFC") \
    X(D, intpl_ir1, scene_setup, OP_INTPL_SF1_LM1, 9, CANARY_IR, "INTPL IR1") \
    X(D, intpl_ir2, scene_setup, OP_INTPL_SF1_LM1, 10, CANARY_IR, "INTPL IR2") \
    X(D, intpl_ir3, scene_setup, OP_INTPL_SF1_LM1, 11, CANARY_IR, "INTPL IR3") \
    X(D, intpl_ir0, scene_setup, OP_INTPL_SF1_LM1, 8, CANARY_IR0, "INTPL IR0") \
    X(C, intpl_rfc, scene_setup, OP_INTPL_SF1_LM1, 21, CANARY_FC, "INTPL RFC") \
    X(C, intpl_gfc, scene_setup, OP_INTPL_SF1_LM1, 22, CANARY_FC, "INTPL GFC") \
    X(C, intpl_bfc, scene_setup, OP_INTPL_SF1_LM1, 23, CANARY_FC, "INTPL BFC") \

CELLS(GWU_FUNCS)

const GwuCell gwu_cells_dpc[] = {CELLS(GWU_ENTRY)};
const int gwu_count_dpc = sizeof(gwu_cells_dpc) / sizeof(gwu_cells_dpc[0]);
