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

#include <stdio.h>

// Address 0 is plain RAM here, so a stray store lands rather than faults.
// The pointer goes through asm so the compiler cannot see it is null and
// turn the access into a trap.

// clang-format off

CESTER_BODY(
    static volatile char *nullByte(void) {
        volatile char *p = 0;
        __asm__("" : "+r"(p));
        return p;
    }
)

// C99 lets vsnprintf take a null buffer when n is 0, and it still has to
// return the length the full output would have had. EASTL's sprintf measures
// that way before formatting, so nothing may land at address 0.

CESTER_TEST(snprintfNullMeasures, test_instance,
    volatile char * zero = nullByte();
    char saved = *zero;
    int r;

    *zero = 'Z';
    r = snprintf(NULL, 0, "%d-%s", 42, "ab");
    cester_assert_int_eq('Z', *zero);
    *zero = saved;
    cester_assert_int_eq(5, r);
)

CESTER_TEST(snprintfZeroSizeWritesNothing, test_instance,
    char b[4] = { 'x', 'x', 'x', 'x' };
    int r;

    r = snprintf(b + 1, 0, "%d-%s", 42, "ab");
    cester_assert_int_eq(5, r);
    cester_assert_int_eq('x', b[0]);
    cester_assert_int_eq('x', b[1]);
)

CESTER_TEST(snprintfTruncates, test_instance,
    char b[4] = { 'x', 'x', 'x', 'x' };
    int r;

    r = snprintf(b, 3, "%d-%s", 42, "ab");
    cester_assert_int_eq(5, r);
    cester_assert_str_equal("42", b);
    cester_assert_int_eq('x', b[3]);
)

CESTER_TEST(snprintfMeasureThenFormat, test_instance,
    volatile char * zero = nullByte();
    char saved = *zero;
    char b[16];
    int n, r;

    *zero = 'Z';
    n = snprintf(NULL, 0, "%d-%s", 42, "ab");
    cester_assert_int_eq('Z', *zero);
    *zero = saved;
    cester_assert_int_eq(5, n);
    r = snprintf(b, n + 1, "%d-%s", 42, "ab");
    cester_assert_int_eq(5, r);
    cester_assert_str_equal("42-ab", b);
)
