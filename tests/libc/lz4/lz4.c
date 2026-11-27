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

/* lz4/lz4.c and lz4/lz4-mips.S against the same blocks: a few hand-built edge
   cases, then random well-formed blocks from lz4gen.h. Every decode runs at
   one of the 16 source/destination alignment pairs, with the output area
   pre-filled and guard bytes on both sides, and is compared byte for byte
   with the raw data the generator built. */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

/* cester includes this file more than once. */
#ifndef LZ4_TEST_GENERATOR
#define LZ4_TEST_GENERATOR
static uint8_t s_raw[192 * 1024], s_comp[192 * 1024];
#define GEN_RAW s_raw
#define GEN_COMP s_comp
#include "lz4gen.h"
#endif

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
    typedef void (*Decompress)(const void *, const void *, void *);
    void lz4_decompress_block(const void *, const void *, void *);
    void lz4_decompress_block_c(const void *, const void *, void *);

    #define GUARD 64
    static uint8_t s_src[192 * 1024 + 4];
    static uint8_t s_out[GUARD + 192 * 1024 + 4 + GUARD];

    /* 0 when the decode at alignment pair k matches, else a bit per check. */
    static int decodeOnce(Decompress fn, const uint8_t *comp, uint32_t complen, const uint8_t *raw,
                          uint32_t rawlen, unsigned k) {
        uint32_t so = (k >> 2) & 3, d = k & 3;
        uint8_t f = (k & 1) ? 0x5a : 0xa5;
        for (uint32_t i = 0; i < complen; i++) s_src[so + i] = comp[i];
        for (uint32_t i = 0; i < GUARD + d + rawlen + GUARD; i++) s_out[i] = f;
        uint8_t *dst = s_out + GUARD + d;
        fn(s_src + so, s_src + so + complen, dst);
        int bad = 0;
        for (uint32_t i = 0; i < rawlen; i++) if (dst[i] != raw[i]) { bad |= 1; break; }
        for (uint32_t i = 0; i < GUARD + d; i++) if (s_out[i] != f) bad |= 2;
        for (uint32_t i = 0; i < GUARD; i++) if (dst[rawlen + i] != f) bad |= 4;
        return bad;
    }

    static int bothAllAlignments(const uint8_t *comp, uint32_t complen, const uint8_t *raw, uint32_t rawlen) {
        int bad = 0;
        for (unsigned k = 0; k < 16; k++) {
            bad |= decodeOnce(lz4_decompress_block_c, comp, complen, raw, rawlen, k);
            bad |= decodeOnce(lz4_decompress_block, comp, complen, raw, rawlen, k) << 4;
        }
        return bad;
    }

    /* Overlapping matches: literals "ab" or "abc" or "a", a match at that
       offset, then 12 literals. */
    static int overlap(uint32_t off, uint32_t ml) {
        static const uint8_t lit[3] = {'a', 'b', 'c'};
        uint8_t comp[64], raw[1100];
        uint32_t c = 0, r = 0, mn = ml - 4;
        comp[c++] = (off << 4) | (mn < 15 ? mn : 15);
        for (uint32_t i = 0; i < off; i++) comp[c++] = raw[r++] = lit[i];
        comp[c++] = off;
        comp[c++] = 0;
        if (mn >= 15) {
            uint32_t n = mn - 15;
            while (n >= 255) { comp[c++] = 255; n -= 255; }
            comp[c++] = n;
        }
        for (uint32_t i = 0; i < ml; i++, r++) raw[r] = raw[r - off];
        comp[c++] = 12 << 4;
        for (uint32_t i = 0; i < 12; i++) comp[c++] = raw[r++] = 'x' + i;
        return bothAllAlignments(comp, c, raw, r);
    }
)

CESTER_TEST(lz4Empty, test_instance,
    static const uint8_t comp[1] = {0};
    cester_assert_int_eq(0, bothAllAlignments(comp, 1, comp, 0));
)

CESTER_TEST(lz4Overlap, test_instance,
    static const uint32_t lengths[] = {4, 5, 7, 8, 9, 18, 19, 20, 33, 273, 274, 275, 1000};
    unsigned failures = 0;
    for (uint32_t off = 1; off <= 3; off++)
        for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
            if (overlap(off, lengths[i])) {
                if (!failures) ramsyscall_printf("  lz4 overlap: first failure offset %u length %u\n", off, lengths[i]);
                failures++;
            }
    cester_assert_uint_eq(0, failures);
)

CESTER_TEST(lz4Random, test_instance,
    unsigned failures = 0;
    genSeed(1);
    for (unsigned n = 0; n < LZ4_RANDOM_BLOCKS; n++) {
        gen();
        int bad = 0;
        bad |= decodeOnce(lz4_decompress_block_c, s_comp, s_complen, s_raw, s_rawlen, n & 15);
        bad |= decodeOnce(lz4_decompress_block, s_comp, s_complen, s_raw, s_rawlen, n & 15) << 4;
        if (bad) {
            if (!failures) ramsyscall_printf("  lz4 random: first failure block %u raw %u checks %x\n", n, s_rawlen, bad);
            failures++;
        }
    }
    cester_assert_uint_eq(0, failures);
)
