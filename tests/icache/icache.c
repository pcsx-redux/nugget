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

/* Instruction cache empirical tests.
 *
 * These tests probe the PS1's 4KB direct-mapped i-cache from real hardware.
 * All cache manipulation runs from KSEG1 (uncached) to avoid Heisenberg
 * effects. All twelve pass on SCPH-5501 and SCPH-1001.
 *
 * The i-cache has 256 lines of 4 words (16 bytes) each. Each line stores:
 *   - 4 code words (32 bits each)
 *   - A tag: physical_addr[31:12] | valid[3:0]
 *
 * Cache line index is determined by address bits [11:4].
 *
 * All tests run code out of s_testArea, an 8KiB buffer in this executable's
 * .bss aligned to 4KiB. TEST_BASE is s_testArea + 0x800 (KSEG0, line 128)
 * and TEST_ALT is TEST_BASE + 0x1000 (line 128 again, next physical page).
 * The buffer belongs to the test, so the code the tests write there never
 * touches kernel RAM (the BIOS relocates kernel code/data to 0x500 and up,
 * and both 0x800 and 0x1800 are kernel code in OpenBIOS and retail BIOS).
 * The uncached mirrors (| 0x20000000) are used for RAM writes that bypass
 * the i-cache.
 *
 * The tag probes take an address, not a line index: the isolated load or
 * store goes to that address' physical mirror, so the tag's match bit is
 * computed against, and tag writes set the address of, TEST_BASE's
 * physical page rather than page 0.
 */

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

#define MIPS_JR_RA    0x03e00008
#define MIPS_NOP      0x00000000

/* Test address: s_testArea + 0x800, KSEG0, line 128 */
#define TEST_BASE     ((uint32_t)s_testArea + 0x800)
#define TEST_BASE_UNC (TEST_BASE | 0x20000000)
#define TEST_LINE     128    /* CACHE_LINE_INDEX(TEST_BASE), checked in fill_pattern */

/* Alternate address: same line, different physical page */
#define TEST_ALT      (TEST_BASE + 0x1000)
#define TEST_ALT_UNC  (TEST_ALT | 0x20000000)

#define CACHE_LINE_INDEX(addr) (((uint32_t)(addr) >> 4) & 0xff)

/* TAG read decoding: low 5 bits carry tag info, rest is code word leakage */
#define TAG_VALID(x)  ((x) & 0x0f)
#define TAG_MATCH(x)  (((x) >> 4) & 1)

/* BIU_CONFIG (0xFFFE0130) values for controlling i-cache refill size.
   IBLKSZ is bits 8-9: 0 = 2-word refill, 1 = 4-word refill (default). */
#define BIU_NORMAL       0x0001e988  /* Default: RAM|DS|IBLKSZ_4|IS1|RDPRI|NOPAD|BGNT|LDSCH */
#define BIU_IBLKSZ_2WORD 0x0001e888  /* 2-word refill: clear IBLKSZ bit 8 */

CESTER_BODY(
    extern void icache_read_tag_line(uint32_t addr, uint32_t *out);
    extern void icache_read_code_line(unsigned line_index, uint32_t *out);
    extern void icache_flush_all_tags(void);
    extern void icache_write_tag(uint32_t addr, uint32_t value);
    extern void icache_write_code_word(unsigned word_index, uint32_t value);
    extern uint32_t icache_read_biu(void);
    extern void icache_fill_with_biu(uint32_t cached_addr, uint32_t biu_value);

    /* Code area for the tests: 8KiB, 4KiB aligned, so that TEST_BASE and
       TEST_ALT both land on line 128 in two different physical pages. */
    static uint32_t s_testArea[2048] __attribute__((aligned(4096)));

    static int s_interruptsWereEnabled;
    static uint32_t s_tagbuf[4];
    static uint32_t s_codebuf[4];
)

CESTER_BEFORE_ALL(icache_tests,
    s_interruptsWereEnabled = enterCriticalSection();
    syscall_flushCache();
)

CESTER_AFTER_ALL(icache_tests,
    syscall_flushCache();
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_BEFORE_EACH(icache_tests, testname, testindex,
)

CESTER_AFTER_EACH(icache_tests, testname, testindex,
)

/* =========================================================================
 * Fill pattern: entry word determines which words get filled.
 *
 * The i-cache fills sequentially from the accessed word to the end of
 * the line. No wrapping. This test verifies the per-word valid bits
 * for all 4 entry points with both IBLKSZ settings.
 *
 * Expected results:
 *   IBLKSZ=0 (2-word): 0x3  0xE  0xC  0x8
 *   IBLKSZ=1 (4-word): 0xF  0xE  0xC  0x8
 * ========================================================================= */
CESTER_TEST(fill_pattern, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;
    unc[4] = MIPS_NOP;
    unc[5] = MIPS_NOP;

    uint32_t biu_vals[] = { BIU_IBLKSZ_2WORD, BIU_NORMAL };
    const char *names[] = { "IBLKSZ=0(2w)", "IBLKSZ=1(4w)" };
    static const uint32_t expected[2][4] = {
        { 0x3, 0xE, 0xC, 0x8 },
        { 0xF, 0xE, 0xC, 0x8 },
    };
    uint32_t valid[2][4];

    cester_assert_uint_eq(TEST_LINE, CACHE_LINE_INDEX(TEST_BASE));
    cester_assert_uint_eq(TEST_LINE, CACHE_LINE_INDEX(TEST_ALT));

    ramsyscall_printf("=== Fill pattern (line %d) ===\n", TEST_LINE);
    ramsyscall_printf("             Word0 Word1 Word2 Word3\n");

    for (int b = 0; b < 2; b++) {
        ramsyscall_printf("%s: ", names[b]);
        for (int word = 0; word < 4; word++) {
            unc[0] = MIPS_NOP;
            unc[1] = MIPS_NOP;
            unc[2] = MIPS_NOP;
            unc[3] = MIPS_NOP;
            unc[word] = MIPS_JR_RA;

            icache_flush_all_tags();
            icache_fill_with_biu((uint32_t)(TEST_BASE + word * 4), biu_vals[b]);
            icache_read_tag_line(TEST_BASE, s_tagbuf);

            valid[b][word] = TAG_VALID(s_tagbuf[0]);
            ramsyscall_printf(" 0x%lx ", valid[b][word]);
        }
        ramsyscall_printf("\n");
    }

    for (int b = 0; b < 2; b++) {
        for (int word = 0; word < 4; word++) {
            cester_assert_uint_eq(expected[b][word], valid[b][word]);
        }
    }
)

/* =========================================================================
 * Tag stores physical address, not virtual.
 *
 * The tag holds the physical page of the fill. Reading the tag at
 * TEST_BASE's physical address, the match bit is set after a fill from
 * TEST_BASE and clear after a fill from TEST_ALT (same line, next page).
 * ========================================================================= */
CESTER_TEST(tag_is_physical, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;
    volatile uint32_t *unc_alt = (volatile uint32_t *)TEST_ALT_UNC;
    unc[0] = MIPS_JR_RA; unc[1] = MIPS_NOP; unc[2] = MIPS_NOP; unc[3] = MIPS_NOP;
    unc_alt[0] = MIPS_JR_RA; unc_alt[1] = MIPS_NOP; unc_alt[2] = MIPS_NOP; unc_alt[3] = MIPS_NOP;

    ramsyscall_printf("=== Tag is physical address ===\n");

    /* Fill from TEST_BASE */
    icache_flush_all_tags();
    ((void (*)(void))TEST_BASE)();
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    uint32_t base_match = TAG_MATCH(s_tagbuf[0]);
    ramsyscall_printf("BASE %08lx: valid=0x%lx match=%ld\n", TEST_BASE,
                      (uint32_t)TAG_VALID(s_tagbuf[0]), base_match);

    /* Fill from TEST_ALT (same line, next physical page) */
    icache_flush_all_tags();
    ((void (*)(void))TEST_ALT)();
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("ALT  %08lx: valid=0x%lx match=%ld\n", TEST_ALT,
                      (uint32_t)TAG_VALID(s_tagbuf[0]), (uint32_t)TAG_MATCH(s_tagbuf[0]));

    cester_assert_uint_eq(1, base_match);
    cester_assert_uint_eq(0, TAG_MATCH(s_tagbuf[0]));
)

/* =========================================================================
 * Valid bits are per-word and fully controllable via TAG write.
 * ========================================================================= */
CESTER_TEST(valid_bits_controllable, icache_tests,
    ramsyscall_printf("=== Valid bits controllable ===\n");

    uint32_t patterns[] = { 0x0, 0x1, 0x2, 0x4, 0x8, 0x5, 0xA, 0xF };
    int n = sizeof(patterns) / sizeof(patterns[0]);

    for (int i = 0; i < n; i++) {
        icache_write_tag(TEST_BASE, patterns[i]);
        icache_read_tag_line(TEST_BASE, s_tagbuf);
        ramsyscall_printf("Wrote 0x%lx -> read 0x%lx\n",
                          patterns[i], (uint32_t)TAG_VALID(s_tagbuf[0]));
        cester_assert_uint_eq(patterns[i], TAG_VALID(s_tagbuf[0]));
    }
)

/* =========================================================================
 * Code words persist after tag invalidation.
 * ========================================================================= */
CESTER_TEST(code_survives_invalidation, icache_tests,
    ramsyscall_printf("=== Code survives invalidation ===\n");

    icache_write_code_word(TEST_LINE * 4 + 0, 0xAAAA0000);
    icache_write_code_word(TEST_LINE * 4 + 1, 0xBBBB1111);
    icache_write_code_word(TEST_LINE * 4 + 2, 0xCCCC2222);
    icache_write_code_word(TEST_LINE * 4 + 3, 0xDDDD3333);
    icache_write_tag(TEST_BASE, 0xF);

    icache_read_code_line(TEST_LINE, s_codebuf);
    ramsyscall_printf("Before: %08lx %08lx %08lx %08lx\n",
                      s_codebuf[0], s_codebuf[1], s_codebuf[2], s_codebuf[3]);

    icache_write_tag(TEST_BASE, 0x0);

    icache_read_code_line(TEST_LINE, s_codebuf);
    ramsyscall_printf("After:  %08lx %08lx %08lx %08lx\n",
                      s_codebuf[0], s_codebuf[1], s_codebuf[2], s_codebuf[3]);

    cester_assert_uint_eq(0xAAAA0000, s_codebuf[0]);
    cester_assert_uint_eq(0xBBBB1111, s_codebuf[1]);
    cester_assert_uint_eq(0xCCCC2222, s_codebuf[2]);
    cester_assert_uint_eq(0xDDDD3333, s_codebuf[3]);
)

/* =========================================================================
 * Stale cache / self-modifying code.
 *
 * The classic test: fill cache with code A, write code B to RAM via
 * uncached mirror (does NOT invalidate cache), execute again. The CPU
 * should serve stale code A from cache. After explicit flush, code B
 * should be served.
 *
 * Also tests per-word valid bit: clearing word 0's valid bit forces
 * a RAM fetch for that word, serving the new code B.
 * ========================================================================= */
CESTER_TEST(stale_cache, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;

    ramsyscall_printf("=== Stale cache / SMC ===\n");

    /* Code A: return 1 */
    unc[0] = 0x24020001;  /* addiu $v0, $zero, 1 */
    unc[1] = MIPS_JR_RA;
    unc[2] = MIPS_NOP;
    unc[3] = MIPS_NOP;

    icache_flush_all_tags();
    int (*fn)(void) = (int (*)(void))TEST_BASE;
    int r1 = fn();

    /* Code B: return 2 (only in RAM) */
    unc[0] = 0x24020002;  /* addiu $v0, $zero, 2 */

    int r2 = fn();  /* should still get 1 (stale) */

    icache_write_tag(TEST_BASE, 0xE);  /* invalidate word 0 only */
    int r3 = fn();  /* should get 2 (fresh from RAM) */

    ramsyscall_printf("Cache hit (stale): %ld (expect 1)\n", (uint32_t)r1);
    ramsyscall_printf("Stale after write: %ld (expect 1)\n", (uint32_t)r2);
    ramsyscall_printf("After invalidate:  %ld (expect 2)\n", (uint32_t)r3);

    cester_assert_uint_eq(1, r1);
    cester_assert_uint_eq(1, r2);
    cester_assert_uint_eq(2, r3);
)

/* =========================================================================
 * RAM writes do not affect the i-cache.
 *
 * Neither KSEG0 (cached) nor KSEG1 (uncached) stores modify i-cache
 * tags or code words. The i-cache is completely independent of the
 * data write path.
 * ========================================================================= */
CESTER_TEST(ram_writes_ignore_icache, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;

    unc[0] = MIPS_JR_RA; unc[1] = MIPS_NOP;
    unc[2] = 0xAAAAAAAA; unc[3] = 0xBBBBBBBB;

    icache_flush_all_tags();
    ((void (*)(void))TEST_BASE)();

    /* Write via KSEG0 (cached store) */
    ((volatile uint32_t *)TEST_BASE)[2] = 0xCCCCCCCC;
    icache_read_code_line(TEST_LINE, s_codebuf);

    ramsyscall_printf("=== RAM writes ignore i-cache ===\n");
    uint32_t after_kseg0 = s_codebuf[2];
    ramsyscall_printf("After KSEG0 write: code[2]=0x%08lx (expect 0xAAAAAAAA)\n", after_kseg0);

    /* Write via KSEG1 (uncached store) */
    unc[2] = 0xDDDDDDDD;
    icache_read_code_line(TEST_LINE, s_codebuf);
    ramsyscall_printf("After KSEG1 write: code[2]=0x%08lx (expect 0xAAAAAAAA)\n", s_codebuf[2]);

    cester_assert_uint_eq(0xAAAAAAAA, after_kseg0);
    cester_assert_uint_eq(0xAAAAAAAA, s_codebuf[2]);
)

/* =========================================================================
 * Consecutive fills OR into valid bits.
 *
 * Filling from word 2 (valid=0xC) then from word 0 (same tag, fills
 * words 0-3) produces valid=0xF. The second fill adds missing words
 * without evicting existing ones.
 * ========================================================================= */
CESTER_TEST(consecutive_fills_compose, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;
    unc[0] = MIPS_JR_RA; unc[1] = MIPS_NOP;
    unc[2] = MIPS_JR_RA; unc[3] = MIPS_NOP;

    ramsyscall_printf("=== Consecutive fills compose ===\n");

    icache_flush_all_tags();
    ((void (*)(void))(TEST_BASE + 8))();  /* fill from word 2 */
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After word-2 fill: valid=0x%lx\n", (uint32_t)TAG_VALID(s_tagbuf[0]));

    ((void (*)(void))TEST_BASE)();  /* fill from word 0, same tag */
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After word-0 fill: valid=0x%lx\n", (uint32_t)TAG_VALID(s_tagbuf[0]));

    cester_assert_uint_eq(0xF, TAG_VALID(s_tagbuf[0]));
)

/* =========================================================================
 * Invalid word with matching tag triggers full line refill.
 *
 * Manually set valid=0xC (words 0,1 invalid), write new code to RAM,
 * then fetch from word 0. The CPU detects the invalid word, refills
 * the ENTIRE line from RAM (not just the invalid words), and sets
 * valid=0xF.
 * ========================================================================= */
CESTER_TEST(invalid_word_full_refill, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;

    /* Version A */
    unc[0] = 0x24020001; unc[1] = MIPS_JR_RA;
    unc[2] = 0xAAAA0002; unc[3] = 0xAAAA0003;

    icache_flush_all_tags();
    int (*fn)(void) = (int (*)(void))TEST_BASE;
    fn();

    uint32_t full_code[4];
    icache_read_code_line(TEST_LINE, full_code);

    /* Version B to RAM */
    unc[0] = 0x24020002; unc[1] = MIPS_JR_RA;
    unc[2] = 0xBBBB0002; unc[3] = 0xBBBB0003;

    /* Invalidate words 0,1 then immediately fetch (no printf in between!) */
    icache_write_tag(TEST_BASE, 0xC);
    int r = fn();

    uint32_t post_valid;
    uint32_t post_code[4];
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    post_valid = TAG_VALID(s_tagbuf[0]);
    icache_read_code_line(TEST_LINE, post_code);

    ramsyscall_printf("=== Invalid word -> full refill ===\n");
    ramsyscall_printf("Returned: %ld (1=stale, 2=fresh)\n", (uint32_t)r);
    ramsyscall_printf("Valid after refill: 0x%lx\n", post_valid);
    ramsyscall_printf("Word 2: 0x%08lx (%s)\n", post_code[2],
                      post_code[2] == 0xBBBB0002 ? "FRESH - full refill" : "STALE");

    cester_assert_uint_eq(2, r);
    cester_assert_uint_eq(0xF, post_valid);
    cester_assert_uint_eq(0xBBBB0002, post_code[2]);
)

/* =========================================================================
 * TAG write overwrites the address portion of the tag.
 *
 * TAG mode stores: (data & 0xF) | (offset & 0xFFFFF000). The write here is
 * at TEST_BASE's physical address, so the tag becomes TEST_BASE's page.
 * The address comes from the write OFFSET, not the data.
 * ========================================================================= */
CESTER_TEST(tag_write_sets_address, icache_tests,
    volatile uint32_t *unc_alt = (volatile uint32_t *)TEST_ALT_UNC;
    unc_alt[0] = MIPS_JR_RA; unc_alt[1] = MIPS_NOP;
    unc_alt[2] = MIPS_NOP; unc_alt[3] = MIPS_NOP;

    ramsyscall_printf("=== TAG write sets address ===\n");

    /* Prime from TEST_ALT (its page won't match a read at TEST_BASE) */
    icache_flush_all_tags();
    ((void (*)(void))TEST_ALT)();
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After fill from ALT: match=%ld\n", (uint32_t)TAG_MATCH(s_tagbuf[0]));
    cester_assert_uint_eq(0, TAG_MATCH(s_tagbuf[0]));

    /* TAG write at line's offset overwrites address to match */
    icache_write_tag(TEST_BASE, 0xF);
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After TAG write:     match=%ld\n", (uint32_t)TAG_MATCH(s_tagbuf[0]));
    cester_assert_uint_eq(1, TAG_MATCH(s_tagbuf[0]));
)

/* =========================================================================
 * IBLKSZ=0 sequential: two 2-word fills compose a full line.
 * ========================================================================= */
CESTER_TEST(iblksz_sequential, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;
    unc[0] = MIPS_JR_RA; unc[1] = MIPS_NOP;
    unc[2] = MIPS_JR_RA; unc[3] = MIPS_NOP;

    ramsyscall_printf("=== IBLKSZ=0 sequential ===\n");

    icache_flush_all_tags();
    icache_fill_with_biu((uint32_t)TEST_BASE, BIU_IBLKSZ_2WORD);
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After word-0 (2w): valid=0x%lx\n", (uint32_t)TAG_VALID(s_tagbuf[0]));

    icache_fill_with_biu((uint32_t)(TEST_BASE + 8), BIU_IBLKSZ_2WORD);
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After word-2 (2w): valid=0x%lx\n", (uint32_t)TAG_VALID(s_tagbuf[0]));

    cester_assert_uint_eq(0xF, TAG_VALID(s_tagbuf[0]));
)

/* =========================================================================
 * Tag mismatch with IBLKSZ=0 evicts and fills only 2 words.
 * ========================================================================= */
CESTER_TEST(iblksz_mismatch, icache_tests,
    volatile uint32_t *unc = (volatile uint32_t *)TEST_BASE_UNC;
    volatile uint32_t *unc_alt = (volatile uint32_t *)TEST_ALT_UNC;
    unc[0] = MIPS_JR_RA; unc[1] = MIPS_NOP; unc[2] = MIPS_NOP; unc[3] = MIPS_NOP;
    unc_alt[0] = MIPS_JR_RA; unc_alt[1] = MIPS_NOP; unc_alt[2] = MIPS_NOP; unc_alt[3] = MIPS_NOP;

    ramsyscall_printf("=== IBLKSZ=0 mismatch ===\n");

    icache_flush_all_tags();
    icache_fill_with_biu((uint32_t)TEST_BASE, BIU_NORMAL);
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After A fill (4w): valid=0x%lx\n", (uint32_t)TAG_VALID(s_tagbuf[0]));

    icache_fill_with_biu((uint32_t)TEST_ALT, BIU_IBLKSZ_2WORD);
    icache_read_tag_line(TEST_BASE, s_tagbuf);
    ramsyscall_printf("After B fill (2w): valid=0x%lx\n", (uint32_t)TAG_VALID(s_tagbuf[0]));

    cester_assert_uint_eq(0x3, TAG_VALID(s_tagbuf[0]));
)

/* =========================================================================
 * BIU_CONFIG default value is 0x0001e988.
 * ========================================================================= */
CESTER_TEST(biu_default, icache_tests,
    uint32_t biu = icache_read_biu();
    ramsyscall_printf("=== BIU_CONFIG: 0x%08lx ===\n", biu);
    cester_assert_uint_eq(BIU_NORMAL, biu);
)
