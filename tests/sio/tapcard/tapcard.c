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

/* Memory cards in the slots of a multitap (SCPH-1070), driven through SIO0
   directly. Run by tests/pcsxrunner/tapcard.cc in pcsx-redux, with one of the
   setup-*.lua scripts beside this file, which leaves the arm in the scratchpad:
     tap:   port 1 has a tap, cards in slots B and D and none in slot C.
     notap: no tap, the slot B and D cards still inserted.
   On a real tap, 82h reads the slot B card exactly as 81h reads slot A's, and
   82h to 84h get no ACK on their first byte when that slot is empty. */

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
static int s_tap;

/* A sector read: 81h (or 82h to 84h), 52h, two zeroes, the address, four
   zeroes, the 128 data bytes, the checksum and the end byte. */
#define FRAME 140
static uint8_t s_reply[FRAME];
static uint8_t s_ack[FRAME];

static void settle(int n) {
    for (; n > 0; n--) __asm__ volatile("");
}

static uint8_t transceive(uint8_t out, uint8_t *ack) {
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

/* Reads sector 0 on port 1 at select byte addr. */
static void readSector0(uint8_t addr) {
    int i;
    SIOS[0].ctrl = SIO_CTRL_DTR;
    SIOS[0].baudRate = 0x88;  /* 250kHz */
    SIOS[0].mode = 0xd;       /* MUL1, 8bit, no parity */
    while (SIOS[0].stat & SIO_STAT_RXRDY) (void)SIOS[0].fifo;
    SIOS[0].ctrl |= SIO_CTRL_TXEN | SIO_CTRL_ACKIRQEN;
    settle(100);
    for (i = 0; i < FRAME; i++) {
        s_reply[i] = transceive(i == 0 ? addr : i == 1 ? 0x52 : 0x00, &s_ack[i]);
        if (!s_ack[i]) break;
    }
    for (i++; i < FRAME; i++) s_reply[i] = s_ack[i] = 0;
    SIOS[0].ctrl = 0;
    settle(100);
    ramsyscall_printf("%02x 52 sector 0:", addr);
    for (i = 0; i < 12; i++) ramsyscall_printf(" %02x%s", s_reply[i], s_ack[i] ? "" : "!");
    ramsyscall_printf(" .. %02x %02x%s\n", s_reply[138], s_reply[139], s_ack[138] ? "" : "!");
}

/* The frame of a formatted card's sector 0: "MC", then zeroes, then 0Eh, the
   xor of the frame. */
static int checkFrame(void) {
    uint8_t sum = 0;
    int i;
    if (s_reply[1] != 0x08 || s_reply[2] != 0x5a || s_reply[3] != 0x5d) return 1;
    if (s_reply[6] != 0x5c || s_reply[7] != 0x5d || s_reply[8] != 0x00 || s_reply[9] != 0x00) return 2;
    if (s_reply[10] != 'M' || s_reply[11] != 'C' || s_reply[137] != 0x0e) return 3;
    for (i = 8; i < 138; i++) sum ^= s_reply[i];
    if (s_reply[138] != sum) return 4;
    if (s_reply[139] != 0x47) return 5;
    for (i = 0; i < 139; i++) {
        if (!s_ack[i]) return 6;
    }
    return 0;
}
)

CESTER_BEFORE_ALL(tapcard_tests,
    volatile uint32_t *arm = (volatile uint32_t *)0x1f800000;
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    s_tap = arm[0] == 0x43504154 && arm[1] == 1;
    ramsyscall_printf("tapcard arm: %08x %08x -> %s\n", arm[0], arm[1], s_tap ? "tap" : "notap");
)

CESTER_AFTER_ALL(tapcard_tests,
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_TEST(tapcardArmIsSet, tapcard_tests,
    volatile uint32_t *arm = (volatile uint32_t *)0x1f800000;
    cester_assert_uint_eq(arm[0], 0x43504154);
)

/* Slot A is the port's own card, tap or not. */
CESTER_TEST(tapcardSlotA, tapcard_tests,
    readSector0(0x81);
    cester_assert_uint_eq(checkFrame(), 0);
)

/* 82h reads the slot B card, as 81h reads slot A's; without a tap, no ACK. */
CESTER_TEST(tapcardSlotB, tapcard_tests,
    readSector0(0x82);
    if (s_tap) {
        cester_assert_uint_eq(checkFrame(), 0);
    } else {
        cester_assert_uint_eq(s_reply[0], 0xff);
        cester_assert_uint_eq(s_ack[0], 0);
    }
)

/* Slot C is empty: no ACK on 83h. */
CESTER_TEST(tapcardSlotCEmpty, tapcard_tests,
    readSector0(0x83);
    cester_assert_uint_eq(s_reply[0], 0xff);
    cester_assert_uint_eq(s_ack[0], 0);
)

/* Only the first byte after select is an address: after 83h finds slot C
   empty, a following 81h does not reach the slot A card. */
CESTER_TEST(tapcardEmptySlotThenA, tapcard_tests,
    uint8_t ack83, ack81;
    SIOS[0].ctrl = SIO_CTRL_DTR;
    SIOS[0].baudRate = 0x88;
    SIOS[0].mode = 0xd;
    while (SIOS[0].stat & SIO_STAT_RXRDY) (void)SIOS[0].fifo;
    SIOS[0].ctrl |= SIO_CTRL_TXEN | SIO_CTRL_ACKIRQEN;
    settle(100);
    (void)transceive(0x83, &ack83);
    (void)transceive(0x81, &ack81);
    SIOS[0].ctrl = 0;
    settle(100);
    ramsyscall_printf("83 then 81: ack %d %d\n", ack83, ack81);
    cester_assert_uint_eq(ack83, 0);
    cester_assert_uint_eq(ack81, 0);
)

/* 84h reads the slot D card past the empty slot C; without a tap, no ACK. */
CESTER_TEST(tapcardSlotD, tapcard_tests,
    readSector0(0x84);
    if (s_tap) {
        cester_assert_uint_eq(checkFrame(), 0);
    } else {
        cester_assert_uint_eq(s_reply[0], 0xff);
        cester_assert_uint_eq(s_ack[0], 0);
    }
)
