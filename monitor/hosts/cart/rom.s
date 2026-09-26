// 
// MIT License
// 
// Copyright (c) 2026 PCSX-Redux authors
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// 

/* Flash cart image for the monitor on a retail BIOS.

   The BIOS calls the pre-boot entry at 0x1f000080 before the kernel is set
   up, which is too early to run anything. The hook arms a cop0 breakpoint on
   any access to 0x80030000, where the BIOS unpacks the shell once the kernel
   is up, points the 0x40 debug vector at `start`, and returns. When the
   breakpoint fires, `start` copies the monitor PS-EXE appended to this image
   into RAM and jumps to it. Same scheme as Unirom's cart images. */

    .set noreorder
    .section .text
    .global _romstart
_romstart:

    .org 0x00
    .word 0
    .string "Not licensed by Sony Computer Entertainment Inc."

    .org 0x80
    .word preboot
    .string "Licensed by Sony Computer Entertainment Inc."

    .org 0x100
preboot:
    la    $t0, revector
    li    $t1, 0x80000040
    lw    $t2, 0($t0)
    lw    $t3, 4($t0)
    lw    $t4, 8($t0)
    lw    $t5, 12($t0)
    sw    $t2, 0($t1)
    sw    $t3, 4($t1)
    sw    $t4, 8($t1)
    sw    $t5, 12($t1)

    li    $t0, 0x80030000
    mtc0  $t0, $5        /* BDA */
    li    $t0, 0xffffffff
    mtc0  $t0, $9        /* BDAM */
    mtc0  $t0, $3        /* BPC */
    mtc0  $t0, $11       /* BPCM */
    li    $t0, 0xeb800000
    mtc0  $t0, $7        /* DCIC: trap on data access to BDA */
    jr    $ra
    nop

revector:
    la    $t0, start
    jr    $t0
    nop

start:
    mtc0  $0, $7
    mtc0  $0, $9

    la    $a0, psexe
    lw    $t0, 0x18($a0)  /* t_addr */
    lw    $t1, 0x1c($a0)  /* t_size */
    addiu $t2, $a0, 0x800
    addiu $t1, 3
    srl   $t1, 2
copy:
    lw    $t3, 0($t2)
    addiu $t2, 4
    sw    $t3, 0($t0)
    addiu $t1, -1
    bnez  $t1, copy
    addiu $t0, 4

    lw    $t0, 0x10($a0)  /* pc0 */
    lw    $sp, 0x30($a0)
    jr    $t0
    move  $fp, $sp

    .align 4
psexe:
    .incbin PSEXE
