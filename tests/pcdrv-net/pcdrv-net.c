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

// PCDRV TCP streams (PCopen("tcp://host:port")). Needs -pcdrv -pcdrvnet and a
// peer on 127.0.0.1:PCDRV_NET_PORT that sends nothing until it has received
// PCDRV_NET_MSG, echoes it back once, then closes. The pcsx-redux gtest
// tests/pcsxrunner/pcdrv-net.cc provides that peer.

#include "common/kernel/pcdrv.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

#define PCDRV_NET_PORT "37373"
#define PCDRV_NET_MSG "pcdrv-net echo 0123456789"
#define PCDRV_NET_MSG_LEN (sizeof(PCDRV_NET_MSG) - 1)
// PCread never blocks, so replies are polled for. Each poll is a trip
// through the host's break handler, which bounds the wait in host time
// rather than in emulated cycles: a few seconds, whatever the CPU core.
#define PCDRV_NET_POLLS 5000000

// clang-format off

CESTER_TEST(pcdrv_net_probe, test_instance,
    int r, fd;

    r = PCinit();
    cester_assert_int_eq(0, r);

    fd = PCopen("probe:net", 0, 0);
    cester_assert_cmp(fd, >=, 0);
    if (fd < 0) return;

    r = PCclose(fd);
    cester_assert_int_eq(0, r);
)

CESTER_TEST(pcdrv_net_refused, test_instance,
    int fd;

    // Only tcp:// is a stream; every other scheme is refused, never a file.
    fd = PCopen("udp://127.0.0.1:" PCDRV_NET_PORT, 0, 0);
    cester_assert_int_eq(-1, fd);
    fd = PCopen("file://pcdrv-net.c", 0, 0);
    cester_assert_int_eq(-1, fd);
)

CESTER_TEST(pcdrv_net_echo, test_instance,
    int r, fd;
    unsigned got = 0, polls, i;
    char buf[64];

    fd = PCopen("tcp://127.0.0.1:" PCDRV_NET_PORT, 0, 0);
    cester_assert_cmp(fd, >=, 0);
    if (fd < 0) return;

    // The peer has sent nothing yet, so this must come back empty, not wait.
    r = PCread(fd, buf, sizeof(buf));
    cester_assert_int_eq(0, r);

    // A stream has no position.
    r = PClseek(fd, 0, PCDRV_SEEK_SET);
    cester_assert_int_eq(-1, r);
    r = PClseek(fd, 0, PCDRV_SEEK_CUR);
    cester_assert_int_eq(-1, r);

    r = PCwrite(fd, PCDRV_NET_MSG, PCDRV_NET_MSG_LEN);
    cester_assert_int_eq(PCDRV_NET_MSG_LEN, r);

    for (polls = 0; (got < PCDRV_NET_MSG_LEN) && (polls < PCDRV_NET_POLLS); polls++) {
        r = PCread(fd, buf + got, sizeof(buf) - got);
        if (r < 0) break;
        got += r;
    }
    ramsyscall_printf("pcdrv-net: echo of %u bytes after %u polls, last PCread %d\n", got, polls, r);
    cester_assert_int_eq(PCDRV_NET_MSG_LEN, got);
    for (i = 0; i < PCDRV_NET_MSG_LEN; i++) {
        cester_assert_char_eq(PCDRV_NET_MSG[i], buf[i]);
    }

    // The peer closes after its one echo: nothing more, then end of stream.
    for (polls = 0; polls < PCDRV_NET_POLLS; polls++) {
        r = PCread(fd, buf, sizeof(buf));
        if (r != 0) break;
    }
    ramsyscall_printf("pcdrv-net: PCread %d after %u polls past the echo\n", r, polls);
    cester_assert_int_eq(-1, r);
    // And it stays there.
    r = PCread(fd, buf, sizeof(buf));
    cester_assert_int_eq(-1, r);

    r = PCclose(fd);
    cester_assert_int_eq(0, r);
)
