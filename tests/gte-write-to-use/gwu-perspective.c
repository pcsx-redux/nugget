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

// Generated from tests/gte-latency-perspective/gte-latency-perspective.c: same scenes, canaries and cells.

#include "gwu.h"


// clang-format off

#ifndef GTE_LATENCY_PERSPECTIVE_HELPERS_DEFINED
#define GTE_LATENCY_PERSPECTIVE_HELPERS_DEFINED

// Test scene for RTPS / RTPT.
//
// V0 = (0x300, 0x400, 0x500) (non-axis-aligned so every R-matrix entry
//                             contributes to the projection)
// V1 = (0x600, 0x700, 0x800)
// V2 = (0x900, 0xA00, 0xB00)
// RT = identity diagonal
// TR = (100, 200, 0x4000)         (Z translation = 0x4000 so Z is well-
//                                  defined, projection division works)
// OFX = (160 << 16), OFY = (120 << 16)
// H = 200
// DQA = 0x100, DQB = 0
//
// Expected RTPS produces a screen-space (SX2, SY2) and depth (SZ3),
// plus depth-cued IR0. Different from baseline when any input changes.
static inline void scene_setup(void) {
    // Rotation matrix R = identity diagonal
    cop2_putc(0, 0x00001000);  // R11R12: R11=0x1000
    cop2_putc(1, 0x00000000);  // R13R21
    cop2_putc(2, 0x00001000);  // R22R23: R22=0x1000
    cop2_putc(3, 0x00000000);  // R31R32
    cop2_putc(4, 0x1000);      // R33

    // Translation
    cop2_putc(5, 100);         // TRX
    cop2_putc(6, 200);         // TRY
    cop2_putc(7, 0x4000);      // TRZ (large enough to keep Z positive)

    // Screen / projection params
    cop2_putc(24, (160 << 16));  // OFX (16.16)
    cop2_putc(25, (120 << 16));  // OFY
    cop2_putc(26, 200);          // H (projection plane Z)
    cop2_putc(27, 0x100);        // DQA (depth-cue scaling)
    cop2_putc(28, 0);            // DQB

    // Vertices
    cop2_put(0, (0x0400u << 16) | 0x0300u); cop2_put(1, 0x00000500);   // V0
    cop2_put(2, (0x0700u << 16) | 0x0600u); cop2_put(3, 0x00000800);   // V1
    cop2_put(4, (0x0A00u << 16) | 0x0900u); cop2_put(5, 0x00000B00);   // V2

    // Clear FIFOs (RTPS pushes one, RTPT pushes three).
    cop2_put(12, 0); cop2_put(13, 0); cop2_put(14, 0);   // SXY FIFO
    cop2_put(16, 0); cop2_put(17, 0); cop2_put(18, 0); cop2_put(19, 0);  // SZ FIFO
    cop2_put(20, 0); cop2_put(21, 0); cop2_put(22, 0);   // RGB FIFO (unused, kept clean)
    cop2_put(24, 0); cop2_put(25, 0); cop2_put(26, 0); cop2_put(27, 0);  // MAC0..3
    cop2_put(8, 0);  // IR0

    cop2_putc(31, 0);  // FLAG
}

#define OP_RTPS_SF1_LM1   COP2_RTPS(1, 1)
#define OP_RTPT_SF1_LM1   COP2_RTPT(1, 1)

#define CANARY_VXY  0x05000400u   // half each (vs 0x300/0x400 in baseline)
#define CANARY_VZ   0x00000400u
#define CANARY_RMAT 0x07ff07ffu
#define CANARY_TR   0x00007fffu
#define CANARY_OF   0x00800000u   // OFX/OFY are 32-bit fixed-point
#define CANARY_H    0x00000080u   // canary H (vs 200 = 0xc8)
#define CANARY_DQA  0x00000040u
#define CANARY_DQB  0x00000080u

#endif // GTE_LATENCY_PERSPECTIVE_HELPERS_DEFINED


#define CELLS(X) \
    X(D, rtps_vxy0, scene_setup, OP_RTPS_SF1_LM1, 0, CANARY_VXY, "RTPS VXY0", "NNNN") \
    X(D, rtps_vz0, scene_setup, OP_RTPS_SF1_LM1, 1, CANARY_VZ, "RTPS VZ0", "NNNN") \
    X(C, rtps_r11r12, scene_setup, OP_RTPS_SF1_LM1, 0, CANARY_RMAT, "RTPS R11R12", "NNNN") \
    X(C, rtps_r13r21, scene_setup, OP_RTPS_SF1_LM1, 1, CANARY_RMAT, "RTPS R13R21", "NNNN") \
    X(C, rtps_r22r23, scene_setup, OP_RTPS_SF1_LM1, 2, CANARY_RMAT, "RTPS R22R23", "NNNN") \
    X(C, rtps_r31r32, scene_setup, OP_RTPS_SF1_LM1, 3, CANARY_RMAT, "RTPS R31R32", "OXNN") \
    X(C, rtps_r33, scene_setup, OP_RTPS_SF1_LM1, 4, CANARY_RMAT, "RTPS R33", "NNNN") \
    X(C, rtps_trx, scene_setup, OP_RTPS_SF1_LM1, 5, CANARY_TR, "RTPS TRX", "NNNN") \
    X(C, rtps_try, scene_setup, OP_RTPS_SF1_LM1, 6, CANARY_TR, "RTPS TRY", "NNNN") \
    X(C, rtps_trz, scene_setup, OP_RTPS_SF1_LM1, 7, CANARY_TR, "RTPS TRZ", "OONN") \
    X(C, rtps_ofx, scene_setup, OP_RTPS_SF1_LM1, 24, CANARY_OF, "RTPS OFX", "NNNN") \
    X(C, rtps_ofy, scene_setup, OP_RTPS_SF1_LM1, 25, CANARY_OF, "RTPS OFY", "NNNN") \
    X(C, rtps_h, scene_setup, OP_RTPS_SF1_LM1, 26, CANARY_H, "RTPS H", "NNNN") \
    X(C, rtps_dqa, scene_setup, OP_RTPS_SF1_LM1, 27, CANARY_DQA, "RTPS DQA", "NNNN") \
    X(C, rtps_dqb, scene_setup, OP_RTPS_SF1_LM1, 28, CANARY_DQB, "RTPS DQB", "NNNN") \
    X(D, rtpt_vxy0, scene_setup, OP_RTPT_SF1_LM1, 0, CANARY_VXY, "RTPT VXY0", "XNNN") \
    X(D, rtpt_vz0, scene_setup, OP_RTPT_SF1_LM1, 1, CANARY_VZ, "RTPT VZ0", "NNNN") \
    X(D, rtpt_vxy1, scene_setup, OP_RTPT_SF1_LM1, 2, CANARY_VXY, "RTPT VXY1", "NNNN") \
    X(D, rtpt_vz1, scene_setup, OP_RTPT_SF1_LM1, 3, CANARY_VZ, "RTPT VZ1", "NNNN") \
    X(D, rtpt_vxy2, scene_setup, OP_RTPT_SF1_LM1, 4, CANARY_VXY, "RTPT VXY2", "NNNN") \
    X(D, rtpt_vz2, scene_setup, OP_RTPT_SF1_LM1, 5, CANARY_VZ, "RTPT VZ2", "NNNN") \
    X(C, rtpt_r11r12, scene_setup, OP_RTPT_SF1_LM1, 0, CANARY_RMAT, "RTPT R11R12", "XNNN") \
    X(C, rtpt_r13r21, scene_setup, OP_RTPT_SF1_LM1, 1, CANARY_RMAT, "RTPT R13R21", "NNNN") \
    X(C, rtpt_r22r23, scene_setup, OP_RTPT_SF1_LM1, 2, CANARY_RMAT, "RTPT R22R23", "NNNN") \
    X(C, rtpt_r31r32, scene_setup, OP_RTPT_SF1_LM1, 3, CANARY_RMAT, "RTPT R31R32", "X?NN") \
    X(C, rtpt_r33, scene_setup, OP_RTPT_SF1_LM1, 4, CANARY_RMAT, "RTPT R33", "NNNN") \
    X(C, rtpt_trx, scene_setup, OP_RTPT_SF1_LM1, 5, CANARY_TR, "RTPT TRX", "XNNN") \
    X(C, rtpt_try, scene_setup, OP_RTPT_SF1_LM1, 6, CANARY_TR, "RTPT TRY", "NNNN") \
    X(C, rtpt_trz, scene_setup, OP_RTPT_SF1_LM1, 7, CANARY_TR, "RTPT TRZ", "X?NN") \
    X(C, rtpt_ofx, scene_setup, OP_RTPT_SF1_LM1, 24, CANARY_OF, "RTPT OFX", "NNNN") \
    X(C, rtpt_ofy, scene_setup, OP_RTPT_SF1_LM1, 25, CANARY_OF, "RTPT OFY", "NNNN") \
    X(C, rtpt_h, scene_setup, OP_RTPT_SF1_LM1, 26, CANARY_H, "RTPT H", "NNNN") \
    X(C, rtpt_dqa, scene_setup, OP_RTPT_SF1_LM1, 27, CANARY_DQA, "RTPT DQA", "NNNN") \
    X(C, rtpt_dqb, scene_setup, OP_RTPT_SF1_LM1, 28, CANARY_DQB, "RTPT DQB", "NNNN") \

CELLS(GWU_FUNCS)

const GwuCell gwu_cells_perspective[] = {CELLS(GWU_ENTRY)};
const int gwu_count_perspective = sizeof(gwu_cells_perspective) / sizeof(gwu_cells_perspective[0]);
