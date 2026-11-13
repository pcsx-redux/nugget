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

// Memory-control delay/size registers 1F801008h..1F80101Ch: does bit 30
// ("Wide DMA") read back? Each register is saved, written with bit 30 set,
// read, and restored before anything touches the BIOS (printf runs from ROM,
// so all printing happens after the restore). Control: DEV0 bit 12 (16-bit
// bus) toggled the same way must read back changed.
//
// SCPH-1000, 1001, 5501, 7001 and 9002: bit 30 reads back as 0 in all six
// registers. Redux keeps it, so that check only runs on hardware.

#include <stdint.h>

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(

#define REG(a) (*(volatile uint32_t *)(a))
static const uint32_t s_addr[7] = {0x1f801008, 0x1f80100c, 0x1f801010, 0x1f801014,
                                   0x1f801018, 0x1f80101c, 0x1f801008};
static uint32_t s_orig[7], s_set[7], s_after[7];

static void probe(int i, uint32_t bit) {
    uint32_t a = s_addr[i];
    uint32_t o = REG(a);
    REG(a) = o ^ bit;
    uint32_t r = REG(a);
    REG(a) = o;
    uint32_t back = REG(a);
    s_orig[i] = o;
    s_set[i] = r;
    s_after[i] = back;
}

static int s_interruptsWereEnabled;

)

CESTER_BEFORE_ALL(dev_wide,
    s_interruptsWereEnabled = enterCriticalSection();
)

CESTER_AFTER_ALL(dev_wide,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(bit30, dev_wide,
    for (int i = 0; i < 6; i++) probe(i, 0x40000000);
    probe(6, 0x00001000);
    for (int i = 0; i < 7; i++)
        ramsyscall_printf("WIDE %08x %s orig %08x written %08x read %08x restored %08x\n", s_addr[i],
                          i == 6 ? "bit12" : "bit30", s_orig[i], s_orig[i] ^ (i == 6 ? 0x1000 : 0x40000000),
                          s_set[i], s_after[i]);
    // control: bit 12 of DEV0 must follow the write
    cester_assert_uint_eq(s_orig[6] ^ 0x1000, s_set[6]);
    for (int i = 0; i < 7; i++) cester_assert_uint_eq(s_orig[i], s_after[i]);
)
// Bit 30 is not stored (runs after bit30, which filled s_set).
CESTER_MAYBE_TEST(bit30_ignored, dev_wide,
    for (int i = 0; i < 6; i++) cester_assert_uint_eq(s_orig[i], s_set[i]);
)
