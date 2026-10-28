/*

MIT License

Copyright (c) 2024 PCSX-Redux authors

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

    .section .text.exceptionHandler, "ax", @progbits
    .set push
    .set noreorder
    .set noat
    .align 2
    .global psyqoExceptionVector
    .global psyqoExceptionHandler
    .global psyqoBreakHandler
    .global psyqoExceptionHandlerStop

/*
VBlank is the common exception, and its only job is to increment the frame
counter. When VBlank is the only pending IRQ, the fast path below increments
it and returns without calling C++. Anything else goes to the C++ handler,
which handles the stacked IRQs, VBlank included.

The fast path only uses $k0 and $k1, so it saves nothing. It reads EPC twice:
once for the interrupted instruction, and again for the return address, since
the frame counter needs $k0 and $k1 in between. The frame counter is
psyqoFrameCounter, defined in gpu.cpp.

The first 4 instructions are not executed from here: takeOverKernel copies them
to the exception vector at 0x80, which leaves 0xa0 onwards for the A0/B0/C0
tables. A conditional branch from 0x80 cannot reach this code when it is linked
more than 128KB away, so the Cause test happens after the jump.

Interrupts vs GTE commands: if the interrupted instruction is a cop2 command,
it has already executed, so the return address is advanced past it. That is
(insn & 0xfe000000) == 0x4a000000, tested as (insn >> 25) == 0x25.
*/

psyqoExceptionVector:
    mfc0  $k0, $13         /* $k0 = Cause */
    lui   $k1, 0x1f80      /* $k1 = hardware registers base */
    j     .LvectorContinued
    andi  $k0, 0x3c        /* $k0 = ExcCode * 4, 0 for interrupts */

    .balign 16
.LvectorContinued:
    bnez  $k0, .LnotIRQ
    lhu   $k0, 0x1070($k1) /* $k0 = IREG */
    nop
    xori  $k0, 0x7ff       /* $k0 = ACK bitfield */
    sw    $k0, 0x1070($k1) /* ACK the IRQs we are signalling */
    sltiu $k1, $k0, 0x7fe  /* 0 if IREG & 0x7fe is 0, I_STAT bits 11-15 reading 0 */
    bnez  $k1, .LgotIRQs   /* Anything beyond VBlank goes to C++ */
    mfc0  $k1, $14         /* $k1 = EPC */
    lui   $k0, %hi(psyqoFrameCounter)
    lw    $k1, 0($k1)      /* $k1 = interrupted instruction */
    nop
    srl   $k1, 25
    xori  $k1, 0x25        /* $k1 = 0 if it is a cop2 command */
    beqz  $k1, .Lcop2
    lw    $k1, %lo(psyqoFrameCounter)($k0)
    nop
    addiu $k1, 1
    sw    $k1, %lo(psyqoFrameCounter)($k0)
    mfc0  $k0, $14         /* $k0 = EPC, again */
    nop
    jr    $k0
    rfe

.Lcop2:
    nop                    /* load delay: the branch's delay slot loaded $k1 */
    addiu $k1, 1
    sw    $k1, %lo(psyqoFrameCounter)($k0)
    mfc0  $k0, $14
    nop
    addiu $k0, 4           /* Skip the cop2 command, it already executed */
    jr    $k0
    rfe

.LgotIRQs:
    /* $k0 = ACK bitfield, $k1 = EPC */
    sw    $at, 0x100($0)
    sw    $v1, 0x108($0)
    sw    $a0, 0x10c($0)
    lw    $v1, 0($k1)      /* $v1 = interrupted instruction */
    xori  $a0, $k0, 0x7ff  /* $a0 = IREG, passed to the C++ handler */
    srl   $v1, 25
    li    $at, 0x25
    bne   $v1, $at, 1f
    nop
    addiu $k1, 4           /* Skip the cop2 command, it already executed */
1:
    la    $v1, psyqoExceptionHandler
.LcallCPlusPlus:
    /* We want to call into C++ now, so we need to save the rest of the registers */
    sw    $v0, 0x104($0)
    sw    $a1, 0x110($0)
    sw    $a2, 0x114($0)
    sw    $a3, 0x118($0)
    sw    $t0, 0x11c($0)
    sw    $t1, 0x120($0)
    sw    $t2, 0x124($0)
    sw    $t3, 0x128($0)
    sw    $t4, 0x12c($0)
    sw    $t5, 0x130($0)
    sw    $t6, 0x134($0)
    sw    $t7, 0x138($0)
    sw    $t8, 0x140($0)
    sw    $t9, 0x144($0)
    sw    $sp, 0x148($0)
    sw    $ra, 0x14c($0)

    /* Call the C++ exception or break handler while adjusting the stack */
    li    $a1, 0
    jalr  $v1
    li    $sp, 0x1000 - 16

    /* Restore the registers and exit */
    lw    $at, 0x100($0)
    lw    $v0, 0x104($0)
    lw    $v1, 0x108($0)
    lw    $a0, 0x10c($0)
    lw    $a1, 0x110($0)
    lw    $a2, 0x114($0)
    lw    $a3, 0x118($0)
    lw    $t0, 0x11c($0)
    lw    $t1, 0x120($0)
    lw    $t2, 0x124($0)
    lw    $t3, 0x128($0)
    lw    $t4, 0x12c($0)
    lw    $t5, 0x130($0)
    lw    $t6, 0x134($0)
    lw    $t7, 0x138($0)
    lw    $t8, 0x140($0)
    lw    $t9, 0x144($0)
    lw    $sp, 0x148($0)
    lw    $ra, 0x14c($0)
    jr    $k1
    rfe

.LnotIRQ:
    /* Beyond break, psyqo only supports IRQs. */
    mfc0  $k0, $13
    sw    $at, 0x100($0)
    sw    $v1, 0x108($0)
    sw    $a0, 0x10c($0)
    mfc0  $k1, $14
    andi  $k0, 0x3c        /* $k0 = ExcCode * 4 */
    li    $at, 0x24
    bne   $k0, $at, .Lstop
    move  $a0, $k0
    lw    $a0, 0($k1)      /* $a0 = the break instruction */
    la    $v1, psyqoBreakHandler
    srl   $a0, 6
    b     .LsaveMoreRegisters
    addiu $k1, 4

psyqoExceptionHandlerStop:
.Lstop:
    b     .Lstop           /* Infinite loop to stop execution */
    nop                    /* Replaced with self-modifying code when adding crash screen */
.LsaveMoreRegisters:
    sw    $gp, 0x150($0)
    sw    $s0, 0x154($0)
    sw    $s1, 0x158($0)
    sw    $s2, 0x15c($0)
    sw    $s3, 0x160($0)
    sw    $s4, 0x164($0)
    sw    $s5, 0x168($0)
    sw    $s6, 0x16c($0)
    sw    $s7, 0x170($0)
    sw    $fp, 0x174($0)
    b     .LcallCPlusPlus
    srl   $a0, $a0, 2
