    .set push
    .set noreorder
    .set noat

/* 4 KB of straight-line code: running it replaces every i-cache line. */
    .section .text.irqcostSled, "ax", @progbits
    .balign 16
    .global irqcostSled
irqcostSled:
    .rept 1022
    nop
    .endr
    jr    $ra
    nop

/*
uint32_t irqcostGaps(uint16_t* out, uint32_t n, uint32_t cold, uint32_t thresh)
Called through its KSEG1 alias, so the loop itself never touches the i-cache.
Spins on root counter 2; every delta >= thresh is one interrupt, stored to out.
Before each wait, cold != 0 runs the sled from KSEG0.
*/
    .section .text.irqcostGaps, "ax", @progbits
    .balign 16
    .global irqcostGaps
irqcostGaps:
    lui   $t0, 0x1f80
    move  $t9, $ra
    la    $t8, irqcostSled
.Lsample:
    beqz  $a1, .Ldone
    nop
    beqz  $a2, .Lwait
    nop
    jalr  $t8
    nop
.Lwait:
    lhu   $t3, 0x1120($t0)
.Linner:
    lhu   $t1, 0x1120($t0)
    nop
    subu  $t2, $t1, $t3
    andi  $t2, 0xffff
    sltu  $t4, $t2, $a3
    bnez  $t4, .Linner
    move  $t3, $t1
    sh    $t2, 0($a0)
    addiu $a0, 2
    b     .Lsample
    addiu $a1, -1
.Ldone:
    jr    $t9
    nop

/*
uint32_t irqcostNormal(uint32_t n)
Same instruction sequence as .Linner, but the exit tests an iteration count.
Interrupts must be masked. Returns root counter 2 ticks over n iterations.
*/
    .global irqcostNormal
irqcostNormal:
    lui   $t0, 0x1f80
    lhu   $t5, 0x1120($t0)
    move  $t3, $t5
.Lnorm:
    lhu   $t1, 0x1120($t0)
    addiu $a0, -1
    subu  $t2, $t1, $t3
    andi  $t2, 0xffff
    sltu  $t4, $t2, $a3
    bnez  $a0, .Lnorm
    move  $t3, $t1
    subu  $v0, $t1, $t5
    jr    $ra
    andi  $v0, 0xffff
    .set pop
