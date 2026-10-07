/*

MIT License

Copyright (c) 2024 PCSX-Redux authors

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

/* Position-independent i-cache flush, callable from main RAM.
 *
 * Inspired by a main-RAM FlushCache found in the PAL game TOCA World
 * Touring Cars (SLES-02572). Like that one, it runs from uncached memory
 * (KSEG1): a fetch from cached space can itself fill i-cache lines while
 * the tags are being rewritten.
 *
 * The i-cache is 4KB, organized as 256 lines of 4 words (16 bytes) each.
 * Each line has a tag: physical_addr[31:12] | valid[3:0], where the 4
 * valid bits correspond to the 4 words in the line.
 *
 * This routine only clears the tags (setting valid bits to 0 for all
 * lines). The code words remain in cache SRAM but will not be served
 * since no valid bits are set. This is sufficient because the CPU
 * checks valid bits on every fetch and will refill from RAM on a miss.
 *
 * The TOCA approach (tag-only clear) does fewer stores than the retail
 * BIOS approach, which does a two-pass clear (tags then code words).
 * Clearing code words is unnecessary since invalid tags already prevent
 * stale code from being served.
 *
 * As in the TOCA sequence, COP0 Status is cleared before BIU_CONFIG is
 * changed. There are no nops after the COP0 Status or BIU_CONFIG writes;
 * this relies on the slow uncached fetches from RAM to cover the hazards.
 */

    .section .text.flushCache, "ax", @progbits
    .align 2
    .set noreorder
    .global _ZN5psyqo6Kernel10flushCacheEv
    .type _ZN5psyqo6Kernel10flushCacheEv, @function
    .global flushCache
    .type flushCache, @function

_ZN5psyqo6Kernel10flushCacheEv:
flushCache:
    /* Save $ra (bal will clobber it) and COP0 Status register.
       The mfc0 is in the bal's delay slot for efficiency. */
    li    $t1, 0xa0000000
    move  $t6, $ra
    bal   1f
    mfc0  $t0, $12

    /* Trampoline to uncached KSEG1 mirror of this code.
       bal set $ra to the address of label 1. ORing with 0xa0000000
       converts any KUSEG/KSEG0 address to its KSEG1 equivalent.
       We skip ahead 4 instructions (16 bytes) to land after the
       jr's delay slot. */
1:
    or    $t1, $ra, $t1
    addiu $t1, 4 * 4
    jr    $t1

    /* Disable interrupts and clear IsC, before the BIU_CONFIG change.
       This is the jr's delay slot, so it is still fetched from the
       caller's segment; the KSEG1 code starts at the lui below. */
    mtc0  $0, $12

    /* Set BIU_CONFIG to 0x0001e90c: TAG | RAM | IBLKSZ_4 | IS1 |
       RDPRI | NOPAD | BGNT | LDSCH.
       TAG (bit 2) routes isolated stores to the i-cache tag memory.
       IS1 (bit 11) enables i-cache access.
       Every other bit is the normal value, less DS, as in the TOCA
       sequence. We keep the value in $t2 to derive the restore value later. */
    lui   $t5, %hi(BIU_CONFIG)
    li    $t2, 0x0001e90c
    sw    $t2, %lo(BIU_CONFIG)($t5)

    /* Set COP0 Status to IsC (bit 16): isolate cache.
       With TAG set in BIU, stores now go to i-cache tag memory.
       The tag format is: physical_addr[31:12] | valid[3:0].
       Writing 0 at offset N sets tag = (0 & 0xF) | (N & 0xFFFFF000),
       which clears all valid bits (since N < 0x1000, address is 0). */
    li    $t1, 0x10000
    mtc0  $t1, $12

    /* Clear all 256 tags. Each tag is at a 16-byte stride (one per
       line). The loop is unrolled 8x, so each iteration covers 8
       lines (128 bytes). 256 lines / 8 = 32 iterations.
       $t3 counts from 0 to 0x0f80 in steps of 0x80.
       The bne compares before the addiu in the delay slot, so the
       last iteration processes $t3 = 0x0f80, storing at offsets
       0x0f80..0x0ff0, covering lines 248-255. */
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

    /* Un-isolate the cache, before restoring BIU_CONFIG. */
    mtc0  $0, $12

    /* Restore BIU_CONFIG to normal: 0x0001e988.
       0x0001e90c + 0x7c = 0x0001e988.
       This clears TAG (bit 2), sets DS (bit 7), and keeps everything
       else the same: RAM | DS | IBLKSZ_4 | IS1 | RDPRI | NOPAD |
       BGNT | LDSCH. */
    addiu $t2, 0x7c
    sw    $t2, %lo(BIU_CONFIG)($t5)

    /* Restore COP0 Status (re-enables interrupts if they were enabled)
       and return via the saved $ra. */
    mtc0  $t0, $12
    jr    $t6
    nop
