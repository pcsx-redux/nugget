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

/*
 * Command 4Dh on a DualShock is a get/set: the nine byte transfer carries six
 * new motor mapping bytes out and returns the six that were in effect before
 * it. Sending a mapping and then asking again has to come back with what was
 * sent, and a controller that answers "no motors mapped" to every query is
 * lying to any game that unlocks rumble and reads back to confirm.
 *
 *   Send  01h 4Dh 00h aa  bb  cc  dd  ee  ff
 *   Reply Hiz F3h 5Ah aa  bb  cc  dd  ee  ff   <- the OLD aa..ff
 *
 * Two writes are checked, not one, so all six slots have to round-trip: an
 * off-by-one anywhere in the mapping offsets misses a slot at one end and
 * shows up here.
 *
 * Needs an analog pad on port 1. Nothing here touches the motors, so this runs
 * unattended, and it runs the same on hardware with a real DualShock plugged in.
 */

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

/* Reply to the last frame sent. Index n is what came back while byte n went
   out, so [1] is the pad's id byte, [2] is 5Ah, and [3..8] are the six mapping
   bytes the pad held before the frame. */
static uint8_t s_reply[9];

static void settle(int n) {
    for (; n > 0; n--) __asm__ volatile("");
}

static void padDeselect(void) {
    /* Dropping select is what resets the pad's state machine between commands. */
    SIOS[0].ctrl = 0;
    settle(100);
}

static void padSelect(void) {
    SIOS[0].ctrl = SIO_CTRL_DTR;
    SIOS[0].baudRate = 0x88;  /* 250kHz */
    SIOS[0].mode = 0xd;       /* MUL1, 8bit, no parity */
    while (SIOS[0].stat & SIO_STAT_RXRDY) {
        (void)SIOS[0].fifo;   /* throw away anything stale */
    }
    SIOS[0].ctrl |= SIO_CTRL_TXEN;
    settle(100);              /* pads need a moment before the first clock pulse */
}

static uint8_t padTransceive(uint8_t out) {
    SIOS[0].ctrl |= SIO_CTRL_ERRRES;
    IREG = ~IRQ_CONTROLLER;
    SIOS[0].fifo = out;
    while (!(SIOS[0].stat & SIO_STAT_RXRDY)) __asm__ volatile("");
    return SIOS[0].fifo;
}

/* Send a nine byte frame to the pad on port 1 and collect the reply. Frames
   that end early leave 0xff in the tail, which is what an absent pad answers
   with throughout, hence the id byte check in every test below. */
static void padFrame(const uint8_t *out) {
    int i;
    padSelect();
    for (i = 0; i < 9; i++) s_reply[i] = padTransceive(out[i]);
    padDeselect();
}

static void dumpReply(const char *what) {
    int i;
    ramsyscall_printf("%s reply:", what);
    for (i = 0; i < 9; i++) ramsyscall_printf(" %02x", s_reply[i]);
    ramsyscall_printf("\n");
}
)

CESTER_BEFORE_ALL(rumble_tests,
    /* Nothing else may drive SIO0 in the middle of a frame. */
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
)

CESTER_AFTER_ALL(rumble_tests,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

/* =================================================================
 * Getting into config mode at all. Everything below needs it, and an
 * unconfigured or absent pad answers 0xff to the whole frame, so this
 * separates "the mapping is wrong" from "there was never a pad here".
 * ================================================================= */
CESTER_TEST(rumbleEntersConfigMode, rumble_tests,
    static const uint8_t enterConfig[9] = { 0x01, 0x43, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t queryOnly[9]   = { 0x01, 0x4d, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    padFrame(enterConfig);
    dumpReply("43h enter");
    cester_assert_uint_ne(s_reply[1], 0xff);

    /* In config mode the id byte is F3h and the second byte is 5Ah. */
    padFrame(queryOnly);
    dumpReply("4Dh query");
    cester_assert_uint_eq(s_reply[1], 0xf3);
    cester_assert_uint_eq(s_reply[2], 0x5a);
)

/* =================================================================
 * The get/set itself. Send a mapping, ask again, and the answer has to
 * be what was sent rather than the power-on "nothing is mapped".
 * A controller that always replies FFh passes nothing here.
 * ================================================================= */
CESTER_TEST(rumbleMappingReadsBack, rumble_tests,
    static const uint8_t enterConfig[9] = { 0x01, 0x43, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t setFirst[9]    = { 0x01, 0x4d, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff };
    static const uint8_t setSecond[9]   = { 0x01, 0x4d, 0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    static const uint8_t queryOnly[9]   = { 0x01, 0x4d, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    padFrame(enterConfig);
    cester_assert_uint_ne(s_reply[1], 0xff);

    /* Map the small motor to the first byte and the large one to the second. */
    padFrame(setFirst);
    dumpReply("4Dh set A");
    cester_assert_uint_eq(s_reply[1], 0xf3);

    /* Ask again while writing a different mapping. The reply is the first one. */
    padFrame(setSecond);
    dumpReply("4Dh set B");
    cester_assert_uint_eq(s_reply[1], 0xf3);
    cester_assert_uint_eq(s_reply[3], 0x00);
    cester_assert_uint_eq(s_reply[4], 0x01);
    cester_assert_uint_eq(s_reply[5], 0xff);
    cester_assert_uint_eq(s_reply[6], 0xff);
    cester_assert_uint_eq(s_reply[7], 0xff);
    cester_assert_uint_eq(s_reply[8], 0xff);

    /* And once more, so every one of the six slots has carried a value of its
       own. An offset that is out by one drops a slot at one end or the other. */
    padFrame(queryOnly);
    dumpReply("4Dh set C");
    cester_assert_uint_eq(s_reply[1], 0xf3);
    cester_assert_uint_eq(s_reply[3], 0x02);
    cester_assert_uint_eq(s_reply[4], 0x03);
    cester_assert_uint_eq(s_reply[5], 0x04);
    cester_assert_uint_eq(s_reply[6], 0x05);
    cester_assert_uint_eq(s_reply[7], 0x06);
    cester_assert_uint_eq(s_reply[8], 0x07);
)
