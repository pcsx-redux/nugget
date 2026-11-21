/*

MIT License

Copyright (c) 2019 PCSX-Redux authors

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

.include "common/hardware/hwregs.inc"

/* OpenBIOS i-cache flush, called via the A(44h) syscall.
 *
 * This is a simplified variant of common/hardware/flushcache.s. It runs
 * from the BIOS ROM in KSEG1 (uncached), or hops to the KSEG1 alias of
 * its body when linked into RAM.
 *
 * This routine only clears the tags (setting all per-word valid bits
 * to 0). Code words remain in cache SRAM but won't be served since
 * the CPU checks valid bits on every fetch. See the TOCA variant in
 * common/hardware/flushcache.s for a detailed explanation.
 */

    .section .text.flushCache, "ax", @progbits
    .align 2
    .set noreorder
    .global flushCache
    .type flushCache, @function

flushCache:
    /* This routine isolates the cache, so it has to be fetched uncached.
       When linked into RAM (KSEG0) hop to the KSEG1 alias of the body
       first; from ROM the OR is a no-op. */
    la    $t6, 1f
    lui   $t7, 0x2000
    or    $t6, $t6, $t7
    jr    $t6
    nop
1:
    /* Save COP0 Status register. */
    mfc0  $t0, $12
    /* Disable interrupts and clear IsC, before the BIU_CONFIG change. */
    mtc0  $0, $12

    /* Set BIU_CONFIG to 0x0001e90c: TAG | RAM | IBLKSZ_4 | IS1 |
       RDPRI | NOPAD | BGNT | LDSCH, the normal value less DS plus TAG.
       TAG (bit 2) routes isolated stores to i-cache tag memory.
       We keep the value in $t2 to derive the restore value later
       (0x0001e90c + 0x7c = 0x0001e988). */
    lui   $t5, %hi(BIU_CONFIG)
    li    $t2, 0x0001e90c
    sw    $t2, %lo(BIU_CONFIG)($t5)

    /* Set COP0 Status to IsC (bit 16): isolate cache.
       With TAG set in BIU, stores now write to i-cache tag memory.
       Each tag is: physical_addr[31:12] | valid[3:0]. Writing 0 at
       an offset < 0x1000 clears all valid bits and sets address to 0. */
    li    $t1, 0x10000
    mtc0  $t1, $12

    /* Clear all 256 tags at 16-byte stride (one per cache line).
       Unrolled 8x: 8 stores per iteration, 32 iterations total. */
    li    $t3, 0
    li    $t4, 0x0f80

1:
    sw    $0, 0x00($t3)
    sw    $0, 0x10($t3)
    sw    $0, 0x20($t3)
    sw    $0, 0x30($t3)
    sw    $0, 0x40($t3)
    sw    $0, 0x50($t3)
    sw    $0, 0x60($t3)
    sw    $0, 0x70($t3)
    bne   $t3, $t4, 1b
    addiu $t3, 0x80

    /* Un-isolate the cache. */
    mtc0  $0, $12
    /* Restore BIU_CONFIG to normal (0x0001e988): clears TAG, sets DS. */
    addiu $t2, 0x7c
    sw    $t2, %lo(BIU_CONFIG)($t5)
    /* Restore COP0 Status and return. */
    mtc0  $t0, $12
    jr    $ra
    nop

/* The code below is the original retail BIOS FlushCache, kept as a
   reference. It was directly reversed from the retail BIOS and is not
   used by OpenBIOS. The retail version does a two-pass clear:
     Pass 1 (TAG mode): clears tags at 16-byte stride
     Pass 2 (non-TAG mode): clears code words at 4-byte stride
   The second pass is unnecessary since clearing valid bits already
   prevents stale code from being served. The 8 dummy reads from
   0xa0000000 at the end have no documented purpose. */

    .section .text.flushCacheOriginal, "ax", @progbits
    .align 2
    .set reorder
    .global flushCacheOriginal
    .type flushCacheOriginal, @function

flushCacheOriginal:
    mfc0  $t3, $12
    nop

    /* The retail BIOS does a bal here, apparently to make sure it runs
       from the 0xBFC ROM space before reconfiguring BIU_CONFIG. This
       reference copy leaves it out.

       Other differences from retail:
         - COP0 Status is mutated BEFORE changing BIU_CONFIG.
         - Register k0 is left untouched. */

    li    $t1, 0x10000
    mtc0  $t1, $12
    nop
    nop

    /* Pass 1: Clear tags.
       BIU_CONFIG = 0x804 = TAG | IS1. */
    li    $t0, 0x804
    sw    $t0, BIU_CONFIG

    move  $t0, $0
    li    $t2, 0x1000

cache_init_1:
    sw    $0, 0x00($t0)
    sw    $0, 0x10($t0)
    sw    $0, 0x20($t0)
    sw    $0, 0x30($t0)
    sw    $0, 0x40($t0)
    sw    $0, 0x50($t0)
    sw    $0, 0x60($t0)
    sw    $0, 0x70($t0)
    addi  $t0, 0x80
    bne   $t0, $t2, cache_init_1

    mtc0  $0, $12
    nop

    /* Pass 2: Clear code words.
       BIU_CONFIG = 0x800 = IS1 (no TAG). With cache isolated and
       IS1 set, stores go to i-cache code word SRAM. This pass zeros
       all 1024 instruction words at 4-byte stride. */
    li    $t0, 0x800
    sw    $t0, BIU_CONFIG

    mtc0  $t1, $12
    nop
    nop

    move  $t0, $0
    li    $t2, 0x1000

cache_init_2:
    sw    $0, 0x00($t0)
    sw    $0, 0x04($t0)
    sw    $0, 0x08($t0)
    sw    $0, 0x0c($t0)
    sw    $0, 0x10($t0)
    sw    $0, 0x14($t0)
    sw    $0, 0x18($t0)
    sw    $0, 0x1c($t0)
    sw    $0, 0x20($t0)
    sw    $0, 0x24($t0)
    sw    $0, 0x28($t0)
    sw    $0, 0x2c($t0)
    sw    $0, 0x30($t0)
    sw    $0, 0x34($t0)
    sw    $0, 0x38($t0)
    sw    $0, 0x3c($t0)
    sw    $0, 0x40($t0)
    sw    $0, 0x44($t0)
    sw    $0, 0x48($t0)
    sw    $0, 0x4c($t0)
    sw    $0, 0x50($t0)
    sw    $0, 0x54($t0)
    sw    $0, 0x58($t0)
    sw    $0, 0x5c($t0)
    sw    $0, 0x60($t0)
    sw    $0, 0x64($t0)
    sw    $0, 0x68($t0)
    sw    $0, 0x6c($t0)
    sw    $0, 0x70($t0)
    sw    $0, 0x74($t0)
    sw    $0, 0x78($t0)
    sw    $0, 0x7c($t0)
    addi  $t0, 0x80
    bne   $t0, $t2, cache_init_2

    mtc0  $0, $12
    nop

    /* 8 dummy reads from uncached RAM (KSEG1), as the retail BIOS
       does. Their purpose is not documented. */
    li    $t0, 0xa0000000
    lw    $t1, 0($t0)
    lw    $t1, 0($t0)
    lw    $t1, 0($t0)
    lw    $t1, 0($t0)
    lw    $t1, 0($t0)
    lw    $t1, 0($t0)
    lw    $t1, 0($t0)
    lw    $t1, 0($t0)
    nop

    /* Restore BIU_CONFIG to normal operation:
       0x1e988 = RAM | DS | IBLKSZ_4 | IS1 | RDPRI | NOPAD | BGNT | LDSCH */
    li    $t0, 0x1e988
    sw    $t0, BIU_CONFIG

    mtc0  $t3, $12
    nop

    jr    $ra
