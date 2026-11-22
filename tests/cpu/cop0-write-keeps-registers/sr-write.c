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

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
    void srwrite_sr(const uint32_t * in, uint32_t * out);
    void srwrite_cause(const uint32_t * in, uint32_t * out);
    void srwrite_rfe(const uint32_t * in, uint32_t * out);
    void srwrite_cause_value(const uint32_t * in, uint32_t * out);

    static const uint32_t s_in[8] = {
        0x11111111, 0x22222222, 0x33333333, 0x44444444,
        0x55555555, 0x66666666, 0x77777777, 0x88888888,
    };

    static void check(void (*probe)(const uint32_t *, uint32_t *)) {
        uint32_t out[8];
        for (unsigned i = 0; i < 8; i++) out[i] = 0;
        probe(s_in, out);
        for (unsigned i = 0; i < 8; i++) cester_assert_uint_eq(s_in[i], out[i]);
    }
)

CESTER_TEST(sr_write_keeps_registers, sr_write_tests,
    check(srwrite_sr);
)

CESTER_TEST(cause_write_keeps_registers, sr_write_tests,
    check(srwrite_cause);
)

// Cause bits 8 and 9 (IP0, IP1) are writable; bits 10 to 15 are not, and a
// write must not touch the source register either.
CESTER_TEST(cause_write_from_register, sr_write_tests,
    uint32_t in[1] = { 0xff00 };
    uint32_t out[2] = { 0, 0 };
    srwrite_cause_value(in, out);
    uint32_t ip = out[0] & 0x300;
    cester_assert_uint_eq(0x300, ip);
    cester_assert_uint_eq(0xff00, out[1]);
)

CESTER_TEST(rfe_keeps_registers, sr_write_tests,
    check(srwrite_rfe);
)
