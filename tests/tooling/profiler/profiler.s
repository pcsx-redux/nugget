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

# A call tree with known instruction counts, the positive control for the
# emulator's call-path profiler. Every function's count is written next to it,
# counting each executed instruction once, delay slots included. Interrupts are
# masked first so that nothing else runs inside main.
#
# Calls, and self counts summed over them:
#   main  x1  22
#   mid   x2  20   (10 each), each calling leaf twice
#   mid2  x1   8   pops its frame before jr, like unoptimized code
#   leaf  x6 108   (18 each): 4 from mid, 1 from mid2, 1 from main
#   rec   x4  38   rec(3) recursing down to rec(0): 10 + 10 + 10 + 8
#   ping  x2  20   ping(3) -> pong(2) -> ping(1) -> pong(0)
#   pong  x2  18
# 234 instructions in all, from the first instruction of main to its return.
# The nop after each lw $ra is the load delay slot: jr $ra right after the
# load would jump through the old $ra.

    .text
    .set noreorder

    .global main
    .type main, @function
main:
    addiu $sp, $sp, -24
    sw    $ra, 20($sp)
    mfc0  $t0, $12
    addiu $t1, $0, -2
    and   $t0, $t0, $t1
    mtc0  $t0, $12
    jal   mid
    nop
    jal   mid
    nop
    jal   mid2
    nop
    jal   leaf
    nop
    jal   rec
    addiu $a0, $0, 3
    jal   ping
    addiu $a0, $0, 3
    lw    $ra, 20($sp)
    move  $v0, $0
    jr    $ra
    addiu $sp, $sp, 24
    .size main, . - main

# 10 instructions, the frame popped in the delay slot of the return.
    .type mid, @function
mid:
    addiu $sp, $sp, -24
    sw    $ra, 20($sp)
    jal   leaf
    nop
    jal   leaf
    nop
    lw    $ra, 20($sp)
    nop
    jr    $ra
    addiu $sp, $sp, 24
    .size mid, . - mid

# 8 instructions. The frame is popped before the return, so the jr and its
# delay slot run after the frame is gone and still belong to mid2.
    .type mid2, @function
mid2:
    addiu $sp, $sp, -24
    sw    $ra, 16($sp)
    jal   leaf
    nop
    lw    $ra, 16($sp)
    addiu $sp, $sp, 24
    jr    $ra
    nop
    .size mid2, . - mid2

# 18 instructions: no frame, $ra never spilled.
    .type leaf, @function
leaf:
    addiu $t0, $0, 5
.Lleaf_loop:
    addiu $t0, $t0, -1
    bnez  $t0, .Lleaf_loop
    nop
    jr    $ra
    nop
    .size leaf, . - leaf

# 10 instructions when it recurses, 8 for rec(0).
    .type rec, @function
rec:
    addiu $sp, $sp, -24
    sw    $ra, 20($sp)
    beqz  $a0, .Lrec_out
    addiu $a0, $a0, -1
    jal   rec
    nop
.Lrec_out:
    lw    $ra, 20($sp)
    nop
    jr    $ra
    addiu $sp, $sp, 24
    .size rec, . - rec

# Mutual recursion, same shape as rec.
    .type ping, @function
ping:
    addiu $sp, $sp, -24
    sw    $ra, 20($sp)
    beqz  $a0, .Lping_out
    addiu $a0, $a0, -1
    jal   pong
    nop
.Lping_out:
    lw    $ra, 20($sp)
    nop
    jr    $ra
    addiu $sp, $sp, 24
    .size ping, . - ping

    .type pong, @function
pong:
    addiu $sp, $sp, -24
    sw    $ra, 20($sp)
    beqz  $a0, .Lpong_out
    addiu $a0, $a0, -1
    jal   ping
    nop
.Lpong_out:
    lw    $ra, 20($sp)
    nop
    jr    $ra
    addiu $sp, $sp, 24
    .size pong, . - pong
