/*

MIT License

Copyright (c) 2021 PCSX-Redux authors

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
    .global delayslot_load_first_entry
    .type delayslot_load_first_entry, @function

/* void delayslot_load_first_entry(const uint32_t buff[2], uint32_t out[6]);
   The first jal enters a block that has never run, with a load still pending
   from its delay slot. A dynarec compiles that block, finds the pending load
   at its top and has to recompile it with full load delays, storing the new
   code in the block's own lookup slot. The jalr then reaches the block through
   the lookup, and the last jal enters it again with a load pending. */
delayslot_load_first_entry:
    move  $t9, $ra
    li    $t0, 0x1111
    move  $t1, $0

    jal   delayslot_load_first_entry_target
    lw    $t0, 0($a0)
    sw    $v0, 0($a1)
    sw    $t0, 4($a1)

    la    $t2, delayslot_load_first_entry_target
    jalr  $t2
    nop
    sw    $v0, 8($a1)

    li    $t0, 0x2222
    jal   delayslot_load_first_entry_target
    lw    $t0, 4($a0)
    sw    $v0, 12($a1)
    sw    $t0, 16($a1)
    sw    $t1, 20($a1)

    jr    $t9
    nop

delayslot_load_first_entry_target:
    move  $v0, $t0
    addiu $t1, $t1, 1
    jr    $ra
    nop

    .set pop
