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

// Generated from tests/gte-latency-mvmva/gte-latency-mvmva.c: same scenes, canaries and cells.

#include "gwu.h"


// clang-format off

#ifndef GTE_LATENCY_MVMVA_HELPERS_DEFINED
#define GTE_LATENCY_MVMVA_HELPERS_DEFINED

// Common scene: all matrices identity-diagonal, all CVs non-zero, V0 and
// IR populated with non-axis-aligned values so every matrix entry counts.
static inline void scene_setup(void) {
    // Rotation matrix R = identity diagonal
    cop2_putc(0, 0x00001000); cop2_putc(1, 0x00000000);
    cop2_putc(2, 0x00001000); cop2_putc(3, 0x00000000);
    cop2_putc(4, 0x1000);

    // Translation
    cop2_putc(5, 100); cop2_putc(6, 200); cop2_putc(7, 0x4000);

    // Light matrix LL = identity diagonal
    cop2_putc(8,  0x00001000); cop2_putc(9,  0x00000000);
    cop2_putc(10, 0x00001000); cop2_putc(11, 0x00000000);
    cop2_putc(12, 0x1000);

    // BK
    cop2_putc(13, 0x800); cop2_putc(14, 0x800); cop2_putc(15, 0x800);

    // Color matrix LC = identity diagonal
    cop2_putc(16, 0x00001000); cop2_putc(17, 0x00000000);
    cop2_putc(18, 0x00001000); cop2_putc(19, 0x00000000);
    cop2_putc(20, 0x1000);

    // FC
    cop2_putc(21, 0x1000); cop2_putc(22, 0x1000); cop2_putc(23, 0x1000);

    // V0 (used by RT and LL variants)
    cop2_put(0, (0x0800u << 16) | 0x0600u); cop2_put(1, 0x00000A00);

    // IR (used by LC variant)
    cop2_put(8,  0x0800);   // IR0
    cop2_put(9,  0x0500);
    cop2_put(10, 0x0600);
    cop2_put(11, 0x0700);

    // Other state cleared.
    cop2_put(2, 0); cop2_put(3, 0); cop2_put(4, 0); cop2_put(5, 0);
    cop2_put(6, 0x00808080);
    cop2_put(12, 0); cop2_put(13, 0); cop2_put(14, 0);
    cop2_put(16, 0); cop2_put(17, 0); cop2_put(18, 0); cop2_put(19, 0);
    cop2_put(20, 0); cop2_put(21, 0); cop2_put(22, 0);
    cop2_put(24, 0); cop2_put(25, 0); cop2_put(26, 0); cop2_put(27, 0);

    cop2_putc(31, 0);
}

#define OP_MVMVA_RT_V0_TR  COP2_MVMVA(1, COP2_MX_RT, COP2_V_V0, COP2_CV_TR, 1)
#define OP_MVMVA_LL_V0_BK  COP2_MVMVA(1, COP2_MX_LL, COP2_V_V0, COP2_CV_BK, 1)
#define OP_MVMVA_LC_IR_BK  COP2_MVMVA(1, COP2_MX_LC, COP2_V_IR, COP2_CV_BK, 1)

#define CANARY_VXY  0x05000400u
#define CANARY_VZ   0x00000400u
#define CANARY_IR   0x0123u
#define CANARY_MAT  0x07ff07ffu
#define CANARY_TR   0x00007fffu
#define CANARY_BK   0x00010000u

#endif // GTE_LATENCY_MVMVA_HELPERS_DEFINED


// libgte MulMatrix / RotMatrix idiom: RT * V0, no translation, lm=0.
#define OP_MVMVA_RT_V0_NONE COP2_MVMVA(1, COP2_MX_RT, COP2_V_V0, COP2_CV_NONE, 0)

#define CELLS(X) \
    X(D, mvmva_rt_v0_tr_vxy0, scene_setup, OP_MVMVA_RT_V0_TR, 0, CANARY_VXY, "MVMVA(RT,V0,TR) VXY0", "XXNN") \
    X(D, mvmva_rt_v0_tr_vz0, scene_setup, OP_MVMVA_RT_V0_TR, 1, CANARY_VZ, "MVMVA(RT,V0,TR) VZ0", "NNNN") \
    X(C, mvmva_rt_v0_tr_r11r12, scene_setup, OP_MVMVA_RT_V0_TR, 0, CANARY_MAT, "MVMVA(RT,V0,TR) R11R12", "OXNN") \
    X(C, mvmva_rt_v0_tr_r13r21, scene_setup, OP_MVMVA_RT_V0_TR, 1, CANARY_MAT, "MVMVA(RT,V0,TR) R13R21", "XNNN") \
    X(C, mvmva_rt_v0_tr_r22r23, scene_setup, OP_MVMVA_RT_V0_TR, 2, CANARY_MAT, "MVMVA(RT,V0,TR) R22R23", "NNNN") \
    X(C, mvmva_rt_v0_tr_r31r32, scene_setup, OP_MVMVA_RT_V0_TR, 3, CANARY_MAT, "MVMVA(RT,V0,TR) R31R32", "NNNN") \
    X(C, mvmva_rt_v0_tr_r33, scene_setup, OP_MVMVA_RT_V0_TR, 4, CANARY_MAT, "MVMVA(RT,V0,TR) R33", "NNNN") \
    X(C, mvmva_rt_v0_tr_trx, scene_setup, OP_MVMVA_RT_V0_TR, 5, CANARY_TR, "MVMVA(RT,V0,TR) TRX", "OONN") \
    X(C, mvmva_rt_v0_tr_try, scene_setup, OP_MVMVA_RT_V0_TR, 6, CANARY_TR, "MVMVA(RT,V0,TR) TRY", "ONNN") \
    X(C, mvmva_rt_v0_tr_trz, scene_setup, OP_MVMVA_RT_V0_TR, 7, CANARY_TR, "MVMVA(RT,V0,TR) TRZ", "NNNN") \
    X(D, mvmva_ll_v0_bk_vxy0, scene_setup, OP_MVMVA_LL_V0_BK, 0, CANARY_VXY, "MVMVA(LL,V0,BK) VXY0", "XXNN") \
    X(D, mvmva_ll_v0_bk_vz0, scene_setup, OP_MVMVA_LL_V0_BK, 1, CANARY_VZ, "MVMVA(LL,V0,BK) VZ0", "NNNN") \
    X(C, mvmva_ll_v0_bk_l11l12, scene_setup, OP_MVMVA_LL_V0_BK, 8, CANARY_MAT, "MVMVA(LL,V0,BK) L11L12", "OXNN") \
    X(C, mvmva_ll_v0_bk_l13l21, scene_setup, OP_MVMVA_LL_V0_BK, 9, CANARY_MAT, "MVMVA(LL,V0,BK) L13L21", "XNNN") \
    X(C, mvmva_ll_v0_bk_l22l23, scene_setup, OP_MVMVA_LL_V0_BK, 10, CANARY_MAT, "MVMVA(LL,V0,BK) L22L23", "NNNN") \
    X(C, mvmva_ll_v0_bk_l31l32, scene_setup, OP_MVMVA_LL_V0_BK, 11, CANARY_MAT, "MVMVA(LL,V0,BK) L31L32", "NNNN") \
    X(C, mvmva_ll_v0_bk_l33, scene_setup, OP_MVMVA_LL_V0_BK, 12, CANARY_MAT, "MVMVA(LL,V0,BK) L33", "NNNN") \
    X(C, mvmva_ll_v0_bk_rbk, scene_setup, OP_MVMVA_LL_V0_BK, 13, CANARY_BK, "MVMVA(LL,V0,BK) RBK", "OONN") \
    X(C, mvmva_ll_v0_bk_gbk, scene_setup, OP_MVMVA_LL_V0_BK, 14, CANARY_BK, "MVMVA(LL,V0,BK) GBK", "ONNN") \
    X(C, mvmva_ll_v0_bk_bbk, scene_setup, OP_MVMVA_LL_V0_BK, 15, CANARY_BK, "MVMVA(LL,V0,BK) BBK", "NNNN") \
    X(D, mvmva_lc_ir_bk_ir1, scene_setup, OP_MVMVA_LC_IR_BK, 9, CANARY_IR, "MVMVA(LC,IR,BK) IR1", "OONN") \
    X(D, mvmva_lc_ir_bk_ir2, scene_setup, OP_MVMVA_LC_IR_BK, 10, CANARY_IR, "MVMVA(LC,IR,BK) IR2", "NNNN") \
    X(D, mvmva_lc_ir_bk_ir3, scene_setup, OP_MVMVA_LC_IR_BK, 11, CANARY_IR, "MVMVA(LC,IR,BK) IR3", "NNNN") \
    X(C, mvmva_lc_ir_bk_lr1lr2, scene_setup, OP_MVMVA_LC_IR_BK, 16, CANARY_MAT, "MVMVA(LC,IR,BK) LR1LR2", "OXNN") \
    X(C, mvmva_lc_ir_bk_lr3lg1, scene_setup, OP_MVMVA_LC_IR_BK, 17, CANARY_MAT, "MVMVA(LC,IR,BK) LR3LG1", "XNNN") \
    X(C, mvmva_lc_ir_bk_lg2lg3, scene_setup, OP_MVMVA_LC_IR_BK, 18, CANARY_MAT, "MVMVA(LC,IR,BK) LG2LG3", "NNNN") \
    X(C, mvmva_lc_ir_bk_lb1lb2, scene_setup, OP_MVMVA_LC_IR_BK, 19, CANARY_MAT, "MVMVA(LC,IR,BK) LB1LB2", "NNNN") \
    X(C, mvmva_lc_ir_bk_lb3, scene_setup, OP_MVMVA_LC_IR_BK, 20, CANARY_MAT, "MVMVA(LC,IR,BK) LB3", "NNNN") \
    X(C, mvmva_lc_ir_bk_rbk, scene_setup, OP_MVMVA_LC_IR_BK, 13, CANARY_BK, "MVMVA(LC,IR,BK) RBK", "OONN") \
    X(C, mvmva_lc_ir_bk_gbk, scene_setup, OP_MVMVA_LC_IR_BK, 14, CANARY_BK, "MVMVA(LC,IR,BK) GBK", "ONNN") \
    X(C, mvmva_lc_ir_bk_bbk, scene_setup, OP_MVMVA_LC_IR_BK, 15, CANARY_BK, "MVMVA(LC,IR,BK) BBK", "NNNN") \
    X(D, mvmva_rt_v0_none_vxy0, scene_setup, OP_MVMVA_RT_V0_NONE, 0, CANARY_VXY, "MVMVA(RT,V0,NONE,lm0) VXY0", "XXNN") \
    X(D, mvmva_rt_v0_none_vz0, scene_setup, OP_MVMVA_RT_V0_NONE, 1, CANARY_VZ, "MVMVA(RT,V0,NONE,lm0) VZ0", "NNNN") \
    X(C, mvmva_rt_v0_none_r11r12, scene_setup, OP_MVMVA_RT_V0_NONE, 0, CANARY_MAT, "MVMVA(RT,V0,NONE,lm0) R11R12", "OXNN") \
    X(C, mvmva_rt_v0_none_r13r21, scene_setup, OP_MVMVA_RT_V0_NONE, 1, CANARY_MAT, "MVMVA(RT,V0,NONE,lm0) R13R21", "XNNN") \
    X(C, mvmva_rt_v0_none_r22r23, scene_setup, OP_MVMVA_RT_V0_NONE, 2, CANARY_MAT, "MVMVA(RT,V0,NONE,lm0) R22R23", "NNNN") \
    X(C, mvmva_rt_v0_none_r31r32, scene_setup, OP_MVMVA_RT_V0_NONE, 3, CANARY_MAT, "MVMVA(RT,V0,NONE,lm0) R31R32", "NNNN") \
    X(C, mvmva_rt_v0_none_r33, scene_setup, OP_MVMVA_RT_V0_NONE, 4, CANARY_MAT, "MVMVA(RT,V0,NONE,lm0) R33", "NNNN") \

CELLS(GWU_FUNCS)

const GwuCell gwu_cells_mvmva[] = {CELLS(GWU_ENTRY)};
const int gwu_count_mvmva = sizeof(gwu_cells_mvmva) / sizeof(gwu_cells_mvmva[0]);
