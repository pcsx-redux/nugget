/* Entered straight from the BIOS boot path. Saves every register before
   anything else runs, then calls main() on the stack the BIOS gave us.
   k0 is the base pointer for the save, so its own value is lost. */

    .section .start, "ax", @progbits
    .set noreorder
    .set noat
    .align 2
    .global _start
    .type _start, @function
_start:
    lui   $k0, %hi(g_entryRegs)
    addiu $k0, $k0, %lo(g_entryRegs)
    sw    $at, 4($k0)
    sw    $v0, 8($k0)
    sw    $v1, 12($k0)
    sw    $a0, 16($k0)
    sw    $a1, 20($k0)
    sw    $a2, 24($k0)
    sw    $a3, 28($k0)
    sw    $t0, 32($k0)
    sw    $t1, 36($k0)
    sw    $t2, 40($k0)
    sw    $t3, 44($k0)
    sw    $t4, 48($k0)
    sw    $t5, 52($k0)
    sw    $t6, 56($k0)
    sw    $t7, 60($k0)
    sw    $s0, 64($k0)
    sw    $s1, 68($k0)
    sw    $s2, 72($k0)
    sw    $s3, 76($k0)
    sw    $s4, 80($k0)
    sw    $s5, 84($k0)
    sw    $s6, 88($k0)
    sw    $s7, 92($k0)
    sw    $t8, 96($k0)
    sw    $t9, 100($k0)
    sw    $k1, 108($k0)
    sw    $gp, 112($k0)
    sw    $sp, 116($k0)
    sw    $fp, 120($k0)
    sw    $ra, 124($k0)
    mfhi  $t0
    sw    $t0, 128($k0)
    mflo  $t0
    sw    $t0, 132($k0)
    mfc0  $t0, $12
    nop
    sw    $t0, 136($k0)
    mfc0  $t0, $13
    nop
    sw    $t0, 140($k0)
    mfc0  $t0, $14
    nop
    sw    $t0, 144($k0)
    mfc0  $t0, $7
    nop
    sw    $t0, 148($k0)
    la    $t0, __bss_start
    la    $t1, __bss_end
    beq   $t0, $t1, 2f
    nop
1:
    sw    $zero, 0($t0)
    addiu $t0, $t0, 4
    bne   $t0, $t1, 1b
    nop
2:
    j     main
    nop
