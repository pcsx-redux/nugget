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
    .section .ramtext, "ax", @progbits
    .align 2
    .global pairLoop
    .global pairStart
    .global pairB1
    .global pairB2
    .global pairT1
    .global pairEnd
    .type pairLoop, @function

/* uint32_t pairLoop(uint32_t iterations)

   Each iteration runs a taken branch (B1) whose delay slot holds another
   taken branch (B2). B2's offset is relative to the PC register, which
   already holds B1's target, so after running T1 as its delay slot B2
   lands on the loop's bnez: +1 per iteration. An interrupt that returns
   to B2 runs it on its own, relative to itself, through its own delay
   slot to T2: +0x101 for that iteration. */

pairLoop:
    move  $v0, $0
    li    $t1, 1
pairStart:
    addiu $a0, -1
pairB1:
    b     pairT1          /* B1: always taken */
pairB2:
    bnez  $t1, pairT2     /* B2: in B1's slot, taken */
    addiu $v0, 1          /* B2's own slot (only runs if B2 is re-run alone) */
pairT1:
    addiu $v0, 1          /* B1's target: runs once, as B2's delay slot */
    addiu $v0, 0x7000     /* never executed */
pairT2:
    addiu $v0, 0x100
    bnez  $a0, pairStart
    nop
pairEnd:
    jr    $ra
    nop

    .set pop
