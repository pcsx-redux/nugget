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

/* The multitap (SCPH-1070) and the shared display block's host input, driven
   through SIO0 directly. Run by tests/pcsxrunner/multitap.cc in pcsx-redux
   with -shmdisplay, and one of the setup-*.lua scripts beside this file, which
   writes the host input fields and leaves the arm in the scratchpad:
     host:   hostInput 1. Port 1 has a tap, with a pad in slots A (Cross),
             B (Triangle) and D (Square) and slot C empty. Port 2 is turned
             into a Mouse by hostDevice, with 300, -5 of motion pending and the
             left button down.
     nohost: every field the same, but hostInput 0: no tap, no mouse, no host
             buttons, which is how input behaves without the block.
     req1:   as host, but a neGcon in slot A and slots B to D empty, the
             setup a real tap was measured with; only the replay of that
             measurement runs.
   The tap follows psx-spx, "Controller and Memory Card Multitap Adaptor". */

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/hardware/sio.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
static int s_interruptsWereEnabled;
static int s_host;
static int s_req1;

/* What the pads answered, byte by byte, and whether each byte was
   acknowledged, which is how a device asks for the next one. */
static uint8_t s_reply[40];
static uint8_t s_ack[40];

static void settle(int n) {
    for (; n > 0; n--) __asm__ volatile("");
}

static void padSelect(int port) {
    SIOS[0].ctrl = SIO_CTRL_DTR | (port ? SIO_CTRL_PORTSEL : 0);
    SIOS[0].baudRate = 0x88;  /* 250kHz */
    SIOS[0].mode = 0xd;       /* MUL1, 8bit, no parity */
    while (SIOS[0].stat & SIO_STAT_RXRDY) {
        (void)SIOS[0].fifo;
    }
    SIOS[0].ctrl |= SIO_CTRL_TXEN | SIO_CTRL_ACKIRQEN;
    settle(100);
}

static void padDeselect(void) {
    /* Dropping select ends the access. */
    SIOS[0].ctrl = 0;
    settle(100);
}

static uint8_t padTransceive(uint8_t out, uint8_t *ack) {
    int i;
    uint8_t in;
    SIOS[0].ctrl |= SIO_CTRL_ERRRES;
    IREG = ~IRQ_CONTROLLER;
    SIOS[0].fifo = out;
    while (!(SIOS[0].stat & SIO_STAT_RXRDY)) __asm__ volatile("");
    in = SIOS[0].fifo;
    *ack = 0;
    for (i = 0; i < 4000; i++) {
        if (IREG & IRQ_CONTROLLER) {
            *ack = 1;
            break;
        }
    }
    return in;
}

/* Sends n bytes on port, whatever the answers. */
static void padFrame(int port, const uint8_t *out, int n) {
    int i;
    padSelect(port);
    for (i = 0; i < n; i++) s_reply[i] = padTransceive(out[i], &s_ack[i]);
    padDeselect();
}

/* A read at address addr, with req as the third byte, and zeroes after. */
static void padRead(int port, uint8_t addr, uint8_t req, int n) {
    uint8_t out[40];
    int i;
    for (i = 0; i < n; i++) out[i] = 0;
    out[0] = addr;
    out[1] = 0x42;
    out[2] = req;
    padFrame(port, out, n);
}

/* The long read: the tap's three bytes, then 42h at the start of each slot's
   block, as psyqo's AdvancedPad sends it. */
static void padReadLong(uint8_t req) {
    uint8_t out[35];
    int i;
    for (i = 0; i < 35; i++) out[i] = 0;
    out[0] = 0x01;
    out[1] = 0x42;
    out[2] = req;
    for (i = 0; i < 4; i++) out[3 + i * 8] = 0x42;
    padFrame(0, out, 35);
}

static void dumpReply(const char *what, int n) {
    int i;
    ramsyscall_printf("%s:", what);
    for (i = 0; i < n; i++) ramsyscall_printf(" %02x%s", s_reply[i], s_ack[i] ? "" : "!");
    ramsyscall_printf("\n");
}
)

CESTER_BEFORE_ALL(multitap_tests,
    volatile uint32_t *arm = (volatile uint32_t *)0x1f800000;
    /* Nothing else may drive SIO0 in the middle of an access. */
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    s_host = arm[0] == 0x5041544d && arm[1] == 1;
    s_req1 = arm[0] == 0x5041544d && arm[1] == 2;
    ramsyscall_printf("multitap arm: %08x %08x -> %s\n", arm[0], arm[1],
                      s_req1 ? "req1" : s_host ? "host" : "nohost");
)

CESTER_AFTER_ALL(multitap_tests,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(multitapArmIsSet, multitap_tests,
    volatile uint32_t *arm = (volatile uint32_t *)0x1f800000;
    cester_assert_uint_eq(arm[0], 0x5041544d);
)

/* "01 42 00 00 .. ;normal read": slot A answers as a pad without a tap would. */
CESTER_TEST(multitapSlotAPlainRead, multitap_tests,
    if (s_req1) return;
    padRead(0, 0x01, 0x00, 5);
    dumpReply("01 42 00", 5);
    cester_assert_uint_eq(s_reply[1], 0x41);
    cester_assert_uint_eq(s_reply[2], 0x5a);
    cester_assert_uint_eq(s_reply[3], 0xff);
    cester_assert_uint_eq(s_reply[4], s_host ? 0xbf : 0xff);
    cester_assert_uint_eq(s_ack[3], 1);
)

/* "sending that byte does NOT affect the current response. Instead, it does
   request that the NEXT command shall return special data". */
CESTER_TEST(multitapLongRead, multitap_tests,
    static const uint8_t slots[4][8] = {
        { 0x41, 0x5a, 0xff, 0xbf, 0xff, 0xff, 0xff, 0xff },
        { 0x41, 0x5a, 0xff, 0xef, 0xff, 0xff, 0xff, 0xff },
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },  /* empty */
        { 0x41, 0x5a, 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff },
    };
    int i;
    if (s_req1) return;

    padRead(0, 0x01, 0x01, 5);
    dumpReply("01 42 01", 5);
    cester_assert_uint_eq(s_reply[1], 0x41);

    padReadLong(0x00);
    dumpReply("long", 35);
    if (!s_host) {
        /* No tap: a plain pad, done after its four bytes. */
        cester_assert_uint_eq(s_reply[1], 0x41);
        cester_assert_uint_eq(s_ack[4], 0);
        return;
    }
    /* "Halfword 0 --> Controller ID for MultiTap (5A80h=Multitap)", then
       4 halfwords per slot, "for empty slots it's padding all 4 halfwords
       with FFFFh". */
    cester_assert_uint_eq(s_reply[1], 0x80);
    cester_assert_uint_eq(s_reply[2], 0x5a);
    for (i = 0; i < 32; i++) {
        cester_assert_uint_eq(s_reply[3 + i], slots[i / 8][i % 8]);
    }
    for (i = 0; i < 34; i++) {
        cester_assert_uint_eq(s_ack[i], 1);
    }
    cester_assert_uint_eq(s_ack[34], 0);

    /* That long read sent 00h as its third byte, so the next read is slot A. */
    padRead(0, 0x01, 0x00, 5);
    dumpReply("after long, REQ=0", 5);
    cester_assert_uint_eq(s_reply[1], 0x41);
)

/* psx-spx says a request made during a long read gets a short garbage
   reply next; a real tap only does that when the host did not send 42h at
   the start of slot A's block (see multitapReq1Replay). With 42h there, as
   psyqo's AdvancedPad sends it, the next one is another long read. */
CESTER_TEST(multitapRequestDuringLongRead, multitap_tests,
    if (s_req1) return;
    padRead(0, 0x01, 0x01, 5);
    padReadLong(0x01);
    cester_assert_uint_eq(s_reply[1], s_host ? 0x80 : 0x41);
    padReadLong(0x00);
    dumpReply("long after long", 35);
    cester_assert_uint_eq(s_reply[1], s_host ? 0x80 : 0x41);
    if (s_host) {
        cester_assert_uint_eq(s_reply[11], 0x41);
        cester_assert_uint_eq(s_reply[14], 0xef);
    }
)

/* "0n 42 00 00 .. ;method 2: receive data from pad number "n" (1..4)". */
CESTER_TEST(multitapSlotAddresses, multitap_tests,
    if (s_req1) return;
    padRead(0, 0x02, 0x00, 5);
    dumpReply("02 42 00", 5);
    if (s_host) {
        cester_assert_uint_eq(s_reply[1], 0x41);
        cester_assert_uint_eq(s_reply[4], 0xef);
    } else {
        cester_assert_uint_eq(s_ack[0], 0);
    }

    padRead(0, 0x04, 0x00, 5);
    dumpReply("04 42 00", 5);
    if (s_host) {
        cester_assert_uint_eq(s_reply[1], 0x41);
        cester_assert_uint_eq(s_reply[4], 0x7f);
    } else {
        cester_assert_uint_eq(s_ack[0], 0);
    }

    /* An empty slot does not answer at all. */
    padRead(0, 0x03, 0x00, 5);
    dumpReply("03 42 00", 5);
    cester_assert_uint_eq(s_ack[0], 0);
)

/* Port 2 has no tap, so its addresses 02h-04h stay silent. */
CESTER_TEST(multitapAbsentOnPort2, multitap_tests,
    if (s_req1) return;
    padRead(1, 0x02, 0x00, 5);
    dumpReply("port 2: 02 42 00", 5);
    cester_assert_uint_eq(s_ack[0], 0);
)

/* The mouse on port 2 takes its motion from the host accumulators: what does
   not fit in a byte waits for the next read. */
CESTER_TEST(mouseFromHost, multitap_tests,
    static const uint8_t dx[4] = { 0x7f, 0x7f, 0x2e, 0x00 };
    static const uint8_t dy[4] = { 0xfb, 0x00, 0x00, 0x00 };
    int i;
    if (s_req1) return;
    for (i = 0; i < 4; i++) {
        padRead(1, 0x01, 0x00, 7);
        dumpReply("port 2: 01 42 00", 7);
        if (!s_host) {
            cester_assert_uint_eq(s_reply[1], 0x41);
            cester_assert_uint_eq(s_reply[4], 0xff);
            continue;
        }
        cester_assert_uint_eq(s_reply[1], 0x12);
        cester_assert_uint_eq(s_reply[2], 0x5a);
        cester_assert_uint_eq(s_reply[3], 0xff);
        cester_assert_uint_eq(s_reply[4], 0xf4);  /* left button down */
        cester_assert_uint_eq(s_reply[5], dx[i]);
        cester_assert_uint_eq(s_reply[6], dy[i]);
    }
)

/* Replays a measurement of a real SCPH-1070 with a neGcon (id 23h) in slot A
   and slots B to D empty, on port 1: 36-byte polls of 01 42 REQ and zeroes,
   the last eight also with 42h at the start of each slot's block. Both runs
   gave, poll by poll (L long, S short, G garbage):
     2 x REQ=0           L S
     8 x REQ=1           S L G G G G G G
     1 x REQ=0           G
     8 x REQ=1           S L G G G G G G
     1 x REQ=0           G
     8 x REQ=1, 42h      S L L L L L L L
   A long read after a long read that did not start slot A's block with 42h
   is the psx-spx "Garbage response": "the multitap returns ONLY four bytes,
   like so: Hiz,80h,5Ah,LSB ... and aborts transfer after that four bytes".
   The access before poll 0 was not seen; a short one with REQ=1 stands in. */
CESTER_TEST(multitapReq1Replay, multitap_tests,
    static const char expected[] = "LSSLGGGGGGGSLGGGGGGGSLLLLLLL";
    uint8_t out[36];
    int poll, i;
    if (!s_req1) return;
    padRead(0, 0x01, 0x01, 9);
    for (poll = 0; poll < 28; poll++) {
        const uint8_t req = (poll < 2 || poll == 10 || poll == 19) ? 0x00 : 0x01;
        for (i = 0; i < 36; i++) out[i] = 0;
        out[0] = 0x01;
        out[1] = 0x42;
        out[2] = req;
        if (poll >= 20) {
            for (i = 0; i < 4; i++) out[3 + i * 8] = 0x42;
        }
        padFrame(0, out, 36);
        ramsyscall_printf("req1 poll %d, expected %c, ", poll, expected[poll]);
        dumpReply("got", 36);
        switch (expected[poll]) {
            case 'S':
                cester_assert_uint_eq(s_reply[1], 0x23);
                cester_assert_uint_eq(s_reply[2], 0x5a);
                cester_assert_uint_eq(s_ack[7], 1);
                cester_assert_uint_eq(s_ack[8], 0);
                break;
            case 'L':
                cester_assert_uint_eq(s_reply[1], 0x80);
                cester_assert_uint_eq(s_reply[2], 0x5a);
                cester_assert_uint_eq(s_reply[3], 0x23);
                cester_assert_uint_eq(s_reply[4], 0x5a);
                for (i = 11; i < 35; i++) cester_assert_uint_eq(s_reply[i], 0xff);
                for (i = 0; i < 34; i++) cester_assert_uint_eq(s_ack[i], 1);
                cester_assert_uint_eq(s_ack[34], 0);
                break;
            case 'G':
                cester_assert_uint_eq(s_reply[1], 0x80);
                cester_assert_uint_eq(s_reply[2], 0x5a);
                cester_assert_uint_eq(s_reply[3], 0x23);
                for (i = 0; i < 3; i++) cester_assert_uint_eq(s_ack[i], 1);
                cester_assert_uint_eq(s_ack[3], 0);
                break;
        }
    }
)
