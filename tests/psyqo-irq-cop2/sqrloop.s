    .set push
    .set noreorder
    .set noat
    .section .text.irqcop2Loop, "ax", @progbits
    .global irqcop2Loop
/*
uint32_t irqcop2Loop(volatile uint32_t* frameCount, uint32_t frames, uint32_t* iterations)
Squares IR1 = 3 with a GTE SQR command over and over until frameCount has moved
by frames. Executed twice, the command leaves 81 instead of 9. Returns the
number of wrong results.
*/
irqcop2Loop:
    lw    $t0, 0($a0)
    li    $t1, 3
    move  $v0, $0
    move  $t5, $0
    addu  $t0, $a1
.Lloop:
    mtc2  $t1, $9
    nop
    nop
    .word 0x4aa00428        /* sqr sf=0: IR1 = IR1 * IR1 */
    mfc2  $t2, $9
    addiu $t5, 1
    xori  $t2, 9
    sltu  $t2, $0, $t2
    addu  $v0, $t2
    lw    $t3, 0($a0)
    nop
    bne   $t3, $t0, .Lloop
    nop
    jr    $ra
    sw    $t5, 0($a2)
    .set pop
