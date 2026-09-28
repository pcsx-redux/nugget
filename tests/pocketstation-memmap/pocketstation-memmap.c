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

// PocketStation memory map probe, driven from the PS1 side.
//
// The PocketStation kernel answers BU command 5Bh FUNC 01h ("Get Memory
// Block") by copying up to 80h bytes from an arbitrary address back over
// the card link, and 5Ch FUNC 01h writes the other way. That reaches the
// PocketStation's own address space without running any code on it.
//
// Frame for a read, one row per exchange (reply is on the same row):
//
//     TX  81 5B 01 00 a0 a1 a2 a3 len 00 [00 x len] 00
//     RX  -- FL FF 05 -- -- -- -- --  L2 [data    ] FF
//
// Every frame is dumped raw, then checked: LEN1 must be 05, LEN2 must
// equal len, and the trailer must be FF. A frame failing any of those is
// printed BAD and its data must not be read as a value.
//
// Pass 1 is read-only. It reads each "zerofilled" range from
// sio/pocketstation.md next to the real registers of the same block, at
// power-of-two offsets, so a partial address decode that mirrors a
// readable register shows up as a copy of that register's value.
// Controls: BIOS ROM vectors, kernel RAM, LCD VRAM, all of which must read
// back non-zero and stable across two reads.

#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/hardware/sio.h"
#include "common/syscalls/syscalls.h"

static void busyLoop(int delay) {
    for (; delay >= 0; delay--) __asm__ __volatile__("");
}

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}

static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

// The PocketStation answers in ARM software, so it is slow to /ACK.
static int waitAck(void) {
    volatile int budget = 0x20000;
    while ((IREG & IRQ_CONTROLLER) == 0) {
        if (--budget <= 0) return 0;
    }
    IREG = ~IRQ_CONTROLLER;
    return 1;
}

static uint8_t exchange(uint8_t out, int *acked) {
    busyLoop(60);
    SIOS[0].fifo = out;
    busyLoop(60);
    SIOS[0].ctrl |= SIO_CTRL_ERRRES;
    IREG = ~IRQ_CONTROLLER;
    *acked = waitAck();
    busyLoop(40);
    return SIOS[0].fifo;
}

#define MAXF (12 + 0x80)
static uint8_t s_tx[MAXF];
static uint8_t s_rx[MAXF];
static uint8_t s_ack[MAXF];
static int s_port;

static uint16_t ctrlFor(int port) {
    uint16_t c = SIO_CTRL_TXEN | SIO_CTRL_DTR | SIO_CTRL_ACKIRQEN;
    if (port) c |= SIO_CTRL_PORTSEL;
    return c;
}

// Clock out n bytes of s_tx with the card selected. Returns number acked.
static int transact(int port, int n) {
    uint32_t sr = irqDisable();
    SIOS[0].ctrl = ctrlFor(port);
    busyLoop(300);
    (void)SIOS[0].fifo;
    int acks = 0, a;
    for (int i = 0; i < n; i++) {
        s_rx[i] = exchange(s_tx[i], &a);
        s_ack[i] = a;
        acks += a;
    }
    SIOS[0].ctrl = 0;
    busyLoop(2000);
    irqRestore(sr);
    return acks;
}

static void dumpRaw(int n) {
    ramsyscall_printf("  RAW");
    for (int i = 0; i < n; i++) ramsyscall_printf(" %02x%s", s_rx[i], s_ack[i] ? "" : "!");
    ramsyscall_printf("\n");
}

static int identify(int port) {
    s_tx[0] = 0x81;
    s_tx[1] = 0x58;
    for (int i = 2; i < 6; i++) s_tx[i] = 0;
    int acks = transact(port, 6);
    ramsyscall_printf("ID port %d acks=%d", port, acks);
    dumpRaw(6);
    // PocketStation 58h: FLAG, 02, 01, 01
    return s_rx[2] == 0x02 && s_rx[3] == 0x01 && s_rx[4] == 0x01;
}

// 5Bh FUNC 01h. Returns 1 if the frame is well formed; data in s_rx[10..].
static int readBlock(uint32_t addr, int len, int quiet) {
    int n = 0;
    s_tx[n++] = 0x81;
    s_tx[n++] = 0x5b;
    s_tx[n++] = 0x01;
    s_tx[n++] = 0x00;
    s_tx[n++] = addr & 0xff;
    s_tx[n++] = (addr >> 8) & 0xff;
    s_tx[n++] = (addr >> 16) & 0xff;
    s_tx[n++] = (addr >> 24) & 0xff;
    s_tx[n++] = len;
    s_tx[n++] = 0x00;
    for (int i = 0; i < len; i++) s_tx[n++] = 0x00;
    s_tx[n++] = 0x00;
    transact(s_port, n);
    int ok = s_rx[3] == 0x05 && s_rx[9] == len && s_rx[10 + len] == 0xff;
    if (!ok || !quiet) {
        ramsyscall_printf("RD %08x len=%02x %s", addr, len, ok ? "ok" : "BAD");
        dumpRaw(n);
    }
    return ok;
}

// Read twice; print data once, flag instability.
static void probe(uint32_t addr, int len, const char *tag) {
    static uint8_t first[0x80];
    int ok1 = readBlock(addr, len, 1);
    for (int i = 0; i < len; i++) first[i] = s_rx[10 + i];
    int ok2 = readBlock(addr, len, 1);
    int stable = 1, nz = 0;
    for (int i = 0; i < len; i++) {
        if (first[i] != s_rx[10 + i]) stable = 0;
        if (s_rx[10 + i]) nz++;
    }
    ramsyscall_printf("MEM %08x %02x %-8s %s%s nz=%d :", addr, len, tag, (ok1 && ok2) ? "ok" : "BAD",
                      stable ? "" : " UNSTABLE", nz);
    for (int i = 0; i < len; i++) {
        if ((i & 3) == 0) ramsyscall_printf(" ");
        ramsyscall_printf("%02x", s_rx[10 + i]);
    }
    ramsyscall_printf("\n");
}

struct Probe {
    uint32_t addr;
    uint8_t len;
    const char *tag;
};

static const struct Probe s_pass1[] = {
    // controls
    {0x04000000, 0x20, "ctl-bios"},
    {0x000000c0, 0x20, "ctl-kram"},
    {0x0d000100, 0x80, "ctl-vram"},
    // F_xxx: real regs 06000000..13, 06000100..13F, extra flash 300..3FF
    {0x06000000, 0x20, "F-lo"},
    {0x060000e0, 0x20, "F-z1"},
    {0x06000100, 0x40, "F-bank"},
    {0x06000140, 0x20, "F-z2"},
    {0x060002e0, 0x20, "F-z2"},
    {0x06000400, 0x20, "F-z3"},
    {0x06000500, 0x20, "F-z3"},
    {0x06000800, 0x20, "F-z3"},
    {0x06001000, 0x20, "F-z3"},
    {0x06010000, 0x20, "F-z3"},
    {0x06100000, 0x20, "F-z3"},
    {0x06800000, 0x20, "F-z3"},
    {0x06ffffe0, 0x20, "F-z3"},
    // LCD_xxx: LCD_MODE/CAL at 0D000000, VRAM 0D000100..17F
    {0x0d000000, 0x08, "LCD-lo"},
    {0x0d000008, 0x18, "LCD-z1"},
    {0x0d0000e0, 0x20, "LCD-z1"},
    {0x0d000180, 0x20, "LCD-z2"},
    {0x0d000200, 0x20, "LCD-z2"},
    {0x0d000300, 0x20, "LCD-z2"},
    {0x0d000500, 0x20, "LCD-z2"},
    {0x0d001100, 0x20, "LCD-z2"},
    {0x0d010100, 0x20, "LCD-z2"},
    {0x0d100100, 0x20, "LCD-z2"},
    {0x0d7fffe0, 0x20, "LCD-z2"},
    // BATT_xxx: IOP/DAC/BATT regs 0D800000..23
    {0x0d800000, 0x24, "BATT-lo"},
    {0x0d800024, 0x1c, "BATT-z"},
    {0x0d800040, 0x20, "BATT-z"},
    {0x0d800100, 0x20, "BATT-z"},
    {0x0d801000, 0x20, "BATT-z"},
    {0x0d900000, 0x20, "BATT-z"},
    {0x0dffffe0, 0x20, "BATT-z"},
    // COM_xxx: the real regs are NOT read here (COM_DATA pops the link's
    // own RX byte). Zerofilled range only, last, since a mirror of
    // COM_DATA would desync the frame that reads it.
    {0x0c00001c, 0x04, "COM-z"},
    {0x0c000020, 0x20, "COM-z"},
    {0x0c000100, 0x20, "COM-z"},
    {0x0c001000, 0x20, "COM-z"},
    {0x0c100000, 0x20, "COM-z"},
    {0x0c7fffe0, 0x20, "COM-z"},
    // controls again: the unit must still be answering at the end
    {0x04000000, 0x20, "ctl-bios"},
};

int main(void) {
    ramsyscall_printf("PSMEMMAP-START pass1\n");

    SIOS[0].ctrl = SIO_CTRL_IR;
    busyLoop(10);
    SIOS[0].baudRate = 0x0088;
    SIOS[0].mode = 13;
    SIOS[0].ctrl = 0;
    busyLoop(100);

    // Instrument control: a pad answers 01 42 with an ID byte and /ACKs.
    for (int p = 0; p < 2; p++) {
        s_tx[0] = 0x01;
        s_tx[1] = 0x42;
        for (int i = 2; i < 5; i++) s_tx[i] = 0;
        int acks = transact(p, 5);
        ramsyscall_printf("PAD port %d acks=%d", p, acks);
        dumpRaw(5);
    }

    // A card that is asleep or still booting may miss the first select.
    s_port = -1;
    for (int tries = 0; tries < 20 && s_port < 0; tries++) {
        for (int p = 0; p < 2; p++) {
            if (identify(p) && s_port < 0) s_port = p;
        }
        if (s_port < 0) busyLoop(30000000);
    }
    if (s_port < 0) {
        ramsyscall_printf("PSMEMMAP-FAIL no PocketStation answered 58h\n");
        while (1) __asm__ __volatile__("");
    }
    ramsyscall_printf("PocketStation on port %d\n", s_port);

    // One raw frame so the parse offsets can be checked by eye.
    readBlock(0x04000000, 8, 0);

    for (unsigned i = 0; i < sizeof(s_pass1) / sizeof(s_pass1[0]); i++) {
        probe(s_pass1[i].addr, s_pass1[i].len, s_pass1[i].tag);
    }

    ramsyscall_printf("PSMEMMAP-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
