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


    .set push
    .set noreorder
    .set noat
    .section .text.copbranch, "ax", @progbits
    .align 2

# Each probe runs one raw instruction word with a branch offset of 4, so a
# taken branch lands on the second exit. $v0 collects which paths ran:
#   bit 0: the delay slot
#   bit 1: the fall-through
#   bit 2: the branch target

    .macro PROBE name, insn
    .global \name
    .type \name, @function
\name:
    move  $v0, $0
    .word \insn
    ori   $v0, $v0, 1
    ori   $v0, $v0, 2
    jr    $ra
    nop
    ori   $v0, $v0, 4
    jr    $ra
    nop
    .size \name, . - \name
    .endm

    .macro PROBE_AFTER name, pre1, pre2, insn
    .global \name
    .type \name, @function
\name:
    .word \pre1
    .word \pre2
    move  $v0, $0
    .word \insn
    ori   $v0, $v0, 1
    ori   $v0, $v0, 2
    jr    $ra
    nop
    ori   $v0, $v0, 4
    jr    $ra
    nop
    .size \name, . - \name
    .endm

# bc2f/bc2t issued while RTPT is still running, and right after setting
# GTE FLAG, which raises FLAG.31
    PROBE_AFTER copbranch_rtpt_bc2f, 0x4a280030, 0x00000000, 0x49000004
    PROBE_AFTER copbranch_rtpt_bc2t, 0x4a280030, 0x00000000, 0x49010004
    PROBE_AFTER copbranch_flag_bc2f, 0x3c017fff, 0x48c1f800, 0x49000004
    PROBE_AFTER copbranch_flag_bc2t, 0x3c017fff, 0x48c1f800, 0x49010004

    PROBE copbranch_mfc0,    0x40016000
    PROBE copbranch_beq,     0x10000004
    PROBE copbranch_bne,     0x14000004
    PROBE copbranch_syscall, 0x0000000c

    PROBE copbranch_bc0f,  0x41000004
    PROBE copbranch_bc0t,  0x41010004
    PROBE copbranch_bc0fl, 0x41020004
    PROBE copbranch_bc0tl, 0x41030004
    PROBE copbranch_bc1f,  0x45000004
    PROBE copbranch_bc1t,  0x45010004
    PROBE copbranch_bc1fl, 0x45020004
    PROBE copbranch_bc1tl, 0x45030004
    PROBE copbranch_bc2f,  0x49000004
    PROBE copbranch_bc2t,  0x49010004
    PROBE copbranch_bc2fl, 0x49020004
    PROBE copbranch_bc2tl, 0x49030004
    PROBE copbranch_bc3f,  0x4d000004
    PROBE copbranch_bc3t,  0x4d010004
    PROBE copbranch_bc3fl, 0x4d020004
    PROBE copbranch_bc3tl, 0x4d030004

    .set pop
