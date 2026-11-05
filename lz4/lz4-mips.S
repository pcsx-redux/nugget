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

/* MIPS version of lz4.c, with the same contract (lz4.h): well-formed blocks
   only, no bounds checks, and in-place decompression with the compressed data
   at the tail of the output buffer. Link one of the two, not both. It reads
   the token, offset and length bytes at the same points of the stream as
   lz4.c, so in place it needs the same margin. It writes nothing outside the
   output, and reads only the aligned words holding the bytes it needs.

   Copies of 8 bytes and up align the destination with byte stores, move words
   (lwr/lwl, sw), and finish with bytes; offset 1 runs of 8 and up store a
   replicated word; shorter copies and offsets 2 and 3 go a byte at a time. On
   an SCPH-1001, decompressing a TIM, a PS-EXE and a text file from a cold
   i-cache takes about 24% fewer cycles than lz4.c built with -Os, at 428 bytes
   of code against 172. */

    .section .text.lz4_decompress_block, "ax", @progbits
    .set push
    .set noreorder
    .set noat
    .global lz4_decompress_block
    .type lz4_decompress_block, @function
    .balign 16
lz4_decompress_block:
    move  $v1, $ra
    li    $t8, 255
    li    $t9, 15
.Ltoken:
    lbu   $t0, 0($a0)
    addiu $a0, 1
    srl   $t1, $t0, 4
    beqz  $t1, .Lcheck
    andi  $t7, $t0, 15
    bne   $t1, $t9, .Llitcopy
    move  $t3, $a0
.Llitext:
    lbu   $t2, 0($a0)
    addiu $a0, 1
    addu  $t1, $t2
    beq   $t2, $t8, .Llitext
    move  $t3, $a0
.Llitcopy:
    bal   .Lcopy
    nop
    move  $a0, $t3
.Lcheck:
    sltu  $t2, $a0, $a1
    beqz  $t2, .Lret
    nop
    lbu   $t2, 0($a0)
    lbu   $t4, 1($a0)
    addiu $a0, 2
    sll   $t4, 8
    or    $t2, $t4
    bne   $t7, $t9, .Lmlen
    addiu $t1, $t7, 4
.Lmext:
    lbu   $t4, 0($a0)
    addiu $a0, 1
    addu  $t1, $t4
    beq   $t4, $t8, .Lmext
    nop
.Lmlen:
    sltiu $t4, $t2, 4
    bnez  $t4, .Lshortoff
    subu  $t3, $a2, $t2
    bal   .Lcopy
    nop
    b     .Ltoken
    nop

.Lshortoff:
    li    $t4, 1
    bne   $t2, $t4, .Lmbytes
    sltiu $t4, $t1, 8
    bnez  $t4, .Lmbytes
    lbu   $t6, -1($a2)
    andi  $t5, $a2, 3
    sll   $t4, $t6, 8
    or    $t6, $t4
    sll   $t4, $t6, 16
    beqz  $t5, .Lfwords
    or    $t6, $t4
.Lfalign:
    sb    $t6, 0($a2)
    addiu $a2, 1
    andi  $t5, $a2, 3
    bnez  $t5, .Lfalign
    addiu $t1, -1
.Lfwords:
    srl   $t4, $t1, 2
    andi  $t1, 3
.Lfw:
    sw    $t6, 0($a2)
    addiu $t4, -1
    bnez  $t4, .Lfw
    addiu $a2, 4
    beqz  $t1, .Ltoken
    nop
.Lft:
    sb    $t6, 0($a2)
    addiu $t1, -1
    bnez  $t1, .Lft
    addiu $a2, 1
    b     .Ltoken
    nop

.Lmbytes:
    bal   .Lcbytes
    nop
    b     .Ltoken
    nop

.Lret:
    jr    $v1
    nop

/* Copy t1 bytes from t3 to a2, both advanced; uses t4-t6; returns via ra. */
.Lcopy:
    sltiu $t4, $t1, 8
    bnez  $t4, .Lcbytes
    andi  $t5, $a2, 3
    beqz  $t5, .Lcwords
    nop
.Lcalign:
    lbu   $t6, 0($t3)
    addiu $t3, 1
    addiu $a2, 1
    andi  $t5, $a2, 3
    addiu $t1, -1
    bnez  $t5, .Lcalign
    sb    $t6, -1($a2)
.Lcwords:
    srl   $t4, $t1, 2
    andi  $t1, 3
.Lcw:
    lwr   $t6, 0($t3)
    lwl   $t6, 3($t3)
    addiu $t4, -1
    addiu $t3, 4
    sw    $t6, 0($a2)
    bnez  $t4, .Lcw
    addiu $a2, 4
.Lcbytes:
    beqz  $t1, .Lcdone
    nop
.Lcb:
    lbu   $t6, 0($t3)
    addiu $t3, 1
    addiu $t1, -1
    sb    $t6, 0($a2)
    bnez  $t1, .Lcb
    addiu $a2, 1
.Lcdone:
    jr    $ra
    nop
    .size lz4_decompress_block, . - lz4_decompress_block
    .set pop
