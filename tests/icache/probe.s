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

/* I-Cache probe routines for empirical cache layout testing.
 *
 * All observation code must run from uncached memory (KSEG1) to avoid
 * disturbing the cache state we're trying to observe. Each function
 * trampolines itself to the uncached mirror using the bal trick.
 *
 * BIU_CONFIG (0xfffe0130) bit definitions used here:
 *   Bit 0:  LOCK  - Cache lock mode (when COP0 SR.IsC=1)
 *   Bit 1:  INV   - Cache invalidation mode (when COP0 SR.IsC=1)
 *   Bit 2:  TAG   - Cache tag test mode (when COP0 SR.IsC=1)
 *   Bit 3:  RAM   - Scratchpad mode
 *   Bit 7:  DS    - Enable data cache / scratchpad
 *   Bit 8-9: IBLKSZ - I-cache refill size (0=2words, 1=4words)
 *   Bit 11: IS1   - Enable instruction cache
 *   Bit 13: RDPRI
 *   Bit 14: NOPAD
 *   Bit 15: BGNT
 *   Bit 16: LDSCH
 *
 * COP0 SR (register 12) bits used:
 *   Bit 16: IsC - Isolate cache
 *   Bit 17: SwC - Swap caches (i-cache acts as d-cache)
 */

.set BIU_CONFIG, 0xfffe0130
/* The normal configuration with DS cleared, with and without TAG.
   These values match what PSYQo's flushcache uses (derived from TOCA World Touring Cars). */
.set BIU_TAG_IS1, 0x0001e90c   /* TAG|RAM|IBLKSZ_4|IS1|RDPRI|NOPAD|BGNT|LDSCH */
.set BIU_IS1,     0x0001e908   /* RAM|IBLKSZ_4|IS1|RDPRI|NOPAD|BGNT|LDSCH (no TAG) */
.set BIU_NORMAL,  0x0001e988   /* RAM|DS|IBLKSZ_4|IS1|RDPRI|NOPAD|BGNT|LDSCH */
.set SR_ISC, 0x00010000        /* COP0 SR: Isolate Cache */
.set SR_SWC, 0x00020000        /* COP0 SR: Swap Caches */
.set SR_ISC_SWC, 0x00030000   /* COP0 SR: Isolate + Swap (access i-cache as d-cache) */

    .set noreorder

/* =========================================================================
 * icache_read_tag_line
 *
 * Read all 4 tag words for the cache line holding a given address. With
 * TAG mode in BIU, an isolated load returns the tag of the line selected
 * by address bits [11:4]; the match bit (bit 4) reflects whether the
 * stored tag equals the physical page of the load address. Reading at
 * the 4 word offsets within the line shows the tag structure.
 *
 * Arguments:
 *   a0 = any address in the line; its physical address
 *        (a0 & 0x1ffffff0) is used for the isolated loads, so the match
 *        bit compares the stored tag against a0's physical page
 *   a1 = pointer to 4-word output buffer
 *
 * Clobbers: t0-t7, v0
 * ========================================================================= */
    .section .text.icache_read_tag_line, "ax", @progbits
    .align 2
    .global icache_read_tag_line
    .type icache_read_tag_line, @function

icache_read_tag_line:
    /* Save ra and s-regs we need */
    addiu $sp, $sp, -16
    sw    $ra, 0($sp)
    sw    $s0, 4($sp)
    sw    $s1, 8($sp)

    move  $s0, $a0          /* s0 = line index */
    move  $s1, $a1          /* s1 = output buffer */

    /* Trampoline to uncached mirror */
    li    $t0, 0xa0000000
    bal   1f
    nop
1:
    or    $t0, $ra, $t0
    addiu $t0, 4 * 4        /* skip to instruction after the jr delay slot */
    jr    $t0
    nop

    /* --- Now running from KSEG1 (uncached) --- */

    /* Save COP0 SR */
    mfc0  $t7, $12

    /* Disable interrupts */
    mtc0  $0, $12
    nop
    nop

    /* Set BIU to TAG + IS1 mode */
    lui   $t6, %hi(BIU_CONFIG)
    li    $t0, BIU_TAG_IS1
    sw    $t0, %lo(BIU_CONFIG)($t6)

    /* Isolate cache */
    li    $t0, SR_ISC
    mtc0  $t0, $12
    nop
    nop

    /* Compute base address: physical address of the line */
    li    $t0, 0x1ffffff0
    and   $t0, $s0, $t0

    /* Read 4 words from the cache at offsets 0, 4, 8, 12 within the line */
    lw    $t1, 0($t0)
    lw    $t2, 4($t0)
    lw    $t3, 8($t0)
    lw    $t4, 12($t0)

    /* Un-isolate cache */
    mtc0  $0, $12
    nop
    nop

    /* Restore BIU to normal */
    li    $t0, BIU_NORMAL
    sw    $t0, %lo(BIU_CONFIG)($t6)

    /* Restore COP0 SR */
    mtc0  $t7, $12
    nop
    nop

    /* Store results to output buffer (now safe, cache is un-isolated) */
    sw    $t1, 0($s1)
    sw    $t2, 4($s1)
    sw    $t3, 8($s1)
    sw    $t4, 12($s1)

    /* Restore and return */
    lw    $ra, 0($sp)
    lw    $s0, 4($sp)
    lw    $s1, 8($sp)
    jr    $ra
    addiu $sp, $sp, 16


/* =========================================================================
 * icache_read_code_line
 *
 * Read the 4 code words for a given cache line. Without TAG mode in BIU,
 * isolated reads go to the code word RAM.
 *
 * Arguments:
 *   a0 = cache line index (0-255)
 *   a1 = pointer to 4-word output buffer
 *
 * Clobbers: t0-t7, v0
 * ========================================================================= */
    .section .text.icache_read_code_line, "ax", @progbits
    .align 2
    .global icache_read_code_line
    .type icache_read_code_line, @function

icache_read_code_line:
    addiu $sp, $sp, -16
    sw    $ra, 0($sp)
    sw    $s0, 4($sp)
    sw    $s1, 8($sp)

    move  $s0, $a0
    move  $s1, $a1

    /* Trampoline to uncached mirror */
    li    $t0, 0xa0000000
    bal   1f
    nop
1:
    or    $t0, $ra, $t0
    addiu $t0, 4 * 4
    jr    $t0
    nop

    /* --- Now running from KSEG1 (uncached) --- */

    mfc0  $t7, $12
    mtc0  $0, $12
    nop
    nop

    /* Set BIU to IS1 only (no TAG) - accesses go to code word RAM */
    lui   $t6, %hi(BIU_CONFIG)
    li    $t0, BIU_IS1
    sw    $t0, %lo(BIU_CONFIG)($t6)

    /* Isolate cache */
    li    $t0, SR_ISC
    mtc0  $t0, $12
    nop
    nop

    /* Compute base address: line_index * 16 */
    sll   $t0, $s0, 4

    /* Read 4 code words */
    lw    $t1, 0($t0)
    lw    $t2, 4($t0)
    lw    $t3, 8($t0)
    lw    $t4, 12($t0)

    /* Un-isolate */
    mtc0  $0, $12
    nop
    nop

    /* Restore BIU */
    li    $t0, BIU_NORMAL
    sw    $t0, %lo(BIU_CONFIG)($t6)

    mtc0  $t7, $12
    nop
    nop

    /* Store results */
    sw    $t1, 0($s1)
    sw    $t2, 4($s1)
    sw    $t3, 8($s1)
    sw    $t4, 12($s1)

    lw    $ra, 0($sp)
    lw    $s0, 4($sp)
    lw    $s1, 8($sp)
    jr    $ra
    addiu $sp, $sp, 16


/* =========================================================================
 * icache_write_tag
 *
 * Write a specific value to a cache tag. Useful for controlled experiments.
 * The stored tag takes its valid bits from the data and its address from
 * the store offset, so the tag ends up matching a0's physical page.
 *
 * Arguments:
 *   a0 = any address in the line; its physical address
 *        (a0 & 0x1ffffff0) is used for the isolated store
 *   a1 = value to write to the tag
 * ========================================================================= */
    .section .text.icache_write_tag, "ax", @progbits
    .align 2
    .global icache_write_tag
    .type icache_write_tag, @function

icache_write_tag:
    addiu $sp, $sp, -12
    sw    $ra, 0($sp)
    sw    $s0, 4($sp)
    sw    $s1, 8($sp)

    move  $s0, $a0
    move  $s1, $a1

    li    $t0, 0xa0000000
    bal   1f
    nop
1:
    or    $t0, $ra, $t0
    addiu $t0, 4 * 4
    jr    $t0
    nop

    /* --- Uncached --- */

    mfc0  $t7, $12
    mtc0  $0, $12
    nop
    nop

    lui   $t6, %hi(BIU_CONFIG)
    li    $t0, BIU_TAG_IS1
    sw    $t0, %lo(BIU_CONFIG)($t6)

    li    $t0, SR_ISC
    mtc0  $t0, $12
    nop
    nop

    /* Write tag at the physical address of the line */
    li    $t0, 0x1ffffff0
    and   $t0, $s0, $t0
    sw    $s1, 0($t0)

    mtc0  $0, $12
    nop
    nop

    li    $t0, BIU_NORMAL
    sw    $t0, %lo(BIU_CONFIG)($t6)

    mtc0  $t7, $12
    nop
    nop

    lw    $ra, 0($sp)
    lw    $s0, 4($sp)
    lw    $s1, 8($sp)
    jr    $ra
    addiu $sp, $sp, 12


/* =========================================================================
 * icache_write_code_word
 *
 * Write a specific value to a cache code word.
 *
 * Arguments:
 *   a0 = word index (0-1023)
 *   a1 = value to write
 * ========================================================================= */
    .section .text.icache_write_code_word, "ax", @progbits
    .align 2
    .global icache_write_code_word
    .type icache_write_code_word, @function

icache_write_code_word:
    addiu $sp, $sp, -12
    sw    $ra, 0($sp)
    sw    $s0, 4($sp)
    sw    $s1, 8($sp)

    move  $s0, $a0
    move  $s1, $a1

    li    $t0, 0xa0000000
    bal   1f
    nop
1:
    or    $t0, $ra, $t0
    addiu $t0, 4 * 4
    jr    $t0
    nop

    /* --- Uncached --- */

    mfc0  $t7, $12
    mtc0  $0, $12
    nop
    nop

    lui   $t6, %hi(BIU_CONFIG)
    li    $t0, BIU_IS1
    sw    $t0, %lo(BIU_CONFIG)($t6)

    li    $t0, SR_ISC
    mtc0  $t0, $12
    nop
    nop

    /* Write code word at word_index * 4 */
    sll   $t0, $s0, 2
    sw    $s1, 0($t0)

    mtc0  $0, $12
    nop
    nop

    li    $t0, BIU_NORMAL
    sw    $t0, %lo(BIU_CONFIG)($t6)

    mtc0  $t7, $12
    nop
    nop

    lw    $ra, 0($sp)
    lw    $s0, 4($sp)
    lw    $s1, 8($sp)
    jr    $ra
    addiu $sp, $sp, 12


/* =========================================================================
 * icache_flush_all_tags
 *
 * Zero all 256 cache tags. Equivalent to what FlushCache does for tags.
 * ========================================================================= */
    .section .text.icache_flush_all_tags, "ax", @progbits
    .align 2
    .global icache_flush_all_tags
    .type icache_flush_all_tags, @function

icache_flush_all_tags:
    move  $t5, $ra

    li    $t0, 0xa0000000
    bal   1f
    nop
1:
    or    $t0, $ra, $t0
    addiu $t0, 4 * 4
    jr    $t0
    nop

    /* --- Uncached --- */

    mfc0  $t7, $12
    mtc0  $0, $12
    nop
    nop

    lui   $t6, %hi(BIU_CONFIG)
    li    $t0, BIU_TAG_IS1
    sw    $t0, %lo(BIU_CONFIG)($t6)

    li    $t0, SR_ISC
    mtc0  $t0, $12
    nop
    nop

    li    $t0, 0
    li    $t1, 0x1000
2:
    sw    $0, 0($t0)
    addiu $t0, 16
    bne   $t0, $t1, 2b
    nop

    mtc0  $0, $12
    nop
    nop

    li    $t0, BIU_NORMAL
    sw    $t0, %lo(BIU_CONFIG)($t6)

    mtc0  $t7, $12
    nop
    nop

    jr    $t5
    nop


/* =========================================================================
 * icache_read_biu
 *
 * Read the current BIU_CONFIG register value from uncached space.
 *
 * Returns: v0 = BIU_CONFIG value
 * ========================================================================= */
    .section .text.icache_read_biu, "ax", @progbits
    .align 2
    .global icache_read_biu
    .type icache_read_biu, @function

icache_read_biu:
    move  $t5, $ra

    li    $t0, 0xa0000000
    bal   1f
    nop
1:
    or    $t0, $ra, $t0
    addiu $t0, 4 * 4
    jr    $t0
    nop

    /* --- Uncached --- */
    lui   $t0, %hi(BIU_CONFIG)
    lw    $v0, %lo(BIU_CONFIG)($t0)

    jr    $t5
    nop


/* =========================================================================
 * icache_fill_with_biu
 *
 * Set BIU_CONFIG to a custom value, execute one instruction from a cached
 * address (triggering a fill with the custom BIU settings), then restore.
 * This lets us test IBLKSZ changes.
 *
 * Arguments:
 *   a0 = cached address to execute from (must contain jr $ra; nop)
 *   a1 = BIU value to use during the fill
 * ========================================================================= */
    .section .text.icache_fill_with_biu, "ax", @progbits
    .align 2
    .global icache_fill_with_biu
    .type icache_fill_with_biu, @function

icache_fill_with_biu:
    addiu $sp, $sp, -16
    sw    $ra, 0($sp)
    sw    $s0, 4($sp)
    sw    $s1, 8($sp)

    move  $s0, $a0
    move  $s1, $a1

    li    $t0, 0xa0000000
    bal   1f
    nop
1:
    or    $t0, $ra, $t0
    addiu $t0, 4 * 4
    jr    $t0
    nop

    /* --- Uncached --- */
    /* Save current BIU */
    lui   $t6, %hi(BIU_CONFIG)
    lw    $t7, %lo(BIU_CONFIG)($t6)

    /* Set custom BIU */
    sw    $s1, %lo(BIU_CONFIG)($t6)

    /* Execute from the cached address - triggers a fill with custom BIU.
       jalr sets $ra to our next instruction (in uncached space),
       so the jr $ra in the target returns here. */
    jalr  $s0
    nop

    /* Restore BIU */
    sw    $t7, %lo(BIU_CONFIG)($t6)

    lw    $ra, 0($sp)
    lw    $s0, 4($sp)
    lw    $s1, 8($sp)
    jr    $ra
    addiu $sp, $sp, 16
