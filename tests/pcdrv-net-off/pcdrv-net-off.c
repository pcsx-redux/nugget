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

// PCDRV networking left off: run under -pcdrv without -pcdrvnet. The probe
// and tcp:// must both be refused, and refused names must not turn into
// files under the PCDRV base either. Needs no peer, unlike tests/pcdrv-net.

#include "common/kernel/pcdrv.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_TEST(pcdrv_net_off, test_instance,
    int r, fd;

    r = PCinit();
    cester_assert_int_eq(0, r);

    fd = PCopen("probe:net", 0, 0);
    cester_assert_int_eq(-1, fd);

    fd = PCopen("tcp://127.0.0.1:37373", 0, 0);
    cester_assert_int_eq(-1, fd);

    fd = PCcreat("probe:net", 0);
    cester_assert_int_eq(-1, fd);
)
