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

/* One BCC/SR access from uncached code: write BCC, set SR, run one access,
   clear SR, put BCC back to 0001E988h, restore SR. a0 points at
   struct BccOp { biu, sr, addr, value, kind } (kind 0 sw, 1 sh, 2 sb,
   3 lw). Returns the loaded word for kind 3, else 0. IRQs stay masked
   throughout because SR is zero around the BCC writes. */

    .set noreorder
    .section .text.bcc_op, "ax", @progbits
    .align 2
    .global bcc_op
    .type bcc_op, @function
bcc_op:
    move  $t9, $ra
    bal   1f
    nop
1:
    lui   $t1, 0x1fff
    ori   $t1, $t1, 0xffff
    and   $t0, $ra, $t1
    lui   $t1, 0xa000
    or    $t0, $t0, $t1
    addiu $t0, $t0, 2f - 1b
    jr    $t0
    nop
2:
    mfc0  $t8, $12
    nop
    mtc0  $zero, $12
    nop
    nop
    lw    $t2, 0($a0)
    lw    $t3, 4($a0)
    lw    $t4, 8($a0)
    lw    $t5, 12($a0)
    lw    $t6, 16($a0)
    move  $v0, $zero
    lui   $t1, 0xfffe
    sw    $t2, 0x130($t1)
    nop
    nop
    nop
    nop
    mtc0  $t3, $12
    nop
    nop
    nop
    nop
    addiu $t7, $zero, 1
    beq   $t6, $t7, 4f
    nop
    addiu $t7, $zero, 2
    beq   $t6, $t7, 5f
    nop
    addiu $t7, $zero, 3
    beq   $t6, $t7, 6f
    nop
    sw    $t5, 0($t4)
    b     7f
    nop
4:
    sh    $t5, 0($t4)
    b     7f
    nop
5:
    sb    $t5, 0($t4)
    b     7f
    nop
6:
    lw    $v0, 0($t4)
    nop
7:
    nop
    nop
    nop
    nop
    mtc0  $zero, $12
    nop
    nop
    nop
    nop
    lui   $t2, 0x0001
    ori   $t2, $t2, 0xe988
    sw    $t2, 0x130($t1)
    nop
    nop
    nop
    nop
    mtc0  $t8, $12
    nop
    nop
    jr    $t9
    nop
