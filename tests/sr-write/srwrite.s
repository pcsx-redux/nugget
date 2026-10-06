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
    .section .text.srwrite, "ax", @progbits
    .align 2

# Each probe loads in[0..7] into $t0-$t7, writes a COP0 register back with
# its own value, and stores $t0-$t7 to out[0..7]. With ten guest registers
# live the recompiler has to place some of them in caller-saved host
# registers, which is what the SR/Cause write and RFE paths must preserve.

    .macro LOAD8
    lw    $t0, 0($a0)
    lw    $t1, 4($a0)
    lw    $t2, 8($a0)
    lw    $t3, 12($a0)
    lw    $t4, 16($a0)
    lw    $t5, 20($a0)
    lw    $t6, 24($a0)
    lw    $t7, 28($a0)
    .endm

    .macro STORE8
    sw    $t0, 0($a1)
    sw    $t1, 4($a1)
    sw    $t2, 8($a1)
    sw    $t3, 12($a1)
    sw    $t4, 16($a1)
    sw    $t5, 20($a1)
    sw    $t6, 24($a1)
    sw    $t7, 28($a1)
    .endm

    .global srwrite_sr
    .type srwrite_sr, @function
srwrite_sr:
    mfc0  $v0, $12
    nop
    LOAD8
    mtc0  $v0, $12
    nop
    STORE8
    jr    $ra
    nop
    .size srwrite_sr, . - srwrite_sr

    .global srwrite_cause
    .type srwrite_cause, @function
srwrite_cause:
    mfc0  $v0, $13
    nop
    LOAD8
    mtc0  $v0, $13
    nop
    STORE8
    jr    $ra
    nop
    .size srwrite_cause, . - srwrite_cause

# RFE pops the KU/IE stack, so the previous and old pairs are first set
# equal to the current pair, which makes the pop leave SR unchanged.
    .global srwrite_rfe
    .type srwrite_rfe, @function
srwrite_rfe:
    mfc0  $v0, $12
    nop
    andi  $v1, $v0, 3
    sll   $at, $v1, 2
    or    $v1, $v1, $at
    sll   $at, $v1, 2
    or    $v1, $v1, $at
    li    $at, ~0x3f
    and   $at, $v0, $at
    or    $v1, $v1, $at
    mtc0  $v1, $12
    nop
    LOAD8
    rfe
    STORE8
    mtc0  $v0, $12
    nop
    jr    $ra
    nop
    .size srwrite_rfe, . - srwrite_rfe

    .set pop
