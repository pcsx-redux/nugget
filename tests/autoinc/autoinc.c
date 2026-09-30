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


/*
 * Does the SPU drop 32-bit writes? spu/soundprocessingunitspu.md says it
 * "occasionally seems to miss" them. A sw to an SPU register pair is two
/*
 * Delay/Size bit 13 ("auto increment"). A 32-bit access to a 16-bit device is
 * two bus transactions, and to an 8-bit device four. This checks whether the
 * device address advances between them with bit 13 set and with it clear.
 *
 *   ROM  DEV2 (1F801010h), 8-bit BIOS ROM: lw/lh/lb of BFC00000h.. under the
 *        BIOS value and with bit 13 cleared. With no advance every byte of a
 *        word comes from one address.
 *   SPU  DEV4 (1F801014h), voice 0 ADSR at 1F801C08h/0Ah, seeded with sh
 *        under a safe setting (write delay 15), then one sw, lw, lh or sh
 *        issued under the setting being tested, readback under the safe one.
 *        Settings: 16-bit with and without bit 13, 8-bit with and without.
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

#define DEV2 (*(volatile uint32_t *)0xbf801010)
#define DEV4 (*(volatile uint32_t *)0xbf801014)
#define ROM ((volatile uint8_t *)0xbfc00000)
#define ADSR ((volatile uint16_t *)0xbf801c08)

#define SAFE 0x200931efu

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}
static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

static void delay(void) {
    for (volatile int i = 0; i < 300; i++);
}

static void romArm(const char *name, uint32_t dev2) {
    uint32_t w[4];
    uint16_t h[4];
    uint8_t b[8];
    uint32_t old = DEV2;
    uint32_t sr = irqDisable();
    DEV2 = dev2;
    for (int i = 0; i < 4; i++) w[i] = ((volatile uint32_t *)ROM)[i];
    for (int i = 0; i < 4; i++) h[i] = ((volatile uint16_t *)ROM)[i];
    for (int i = 0; i < 8; i++) b[i] = ROM[i];
    DEV2 = old;
    irqRestore(sr);
    ramsyscall_printf("ROM %-6s dev2=%08x lw %08x %08x %08x %08x lh %04x %04x %04x %04x lb", name, dev2, w[0], w[1],
                      w[2], w[3], h[0], h[1], h[2], h[3]);
    for (int i = 0; i < 8; i++) ramsyscall_printf(" %02x", b[i]);
    ramsyscall_printf("\n");
}

static void seed(uint16_t lo, uint16_t hi) {
    DEV4 = SAFE;
    ADSR[0] = lo;
    ADSR[1] = hi;
    delay();
}

static void spuArm(const char *name, uint32_t dev4) {
    uint32_t sr = irqDisable();
    uint32_t old = DEV4;
    uint16_t swLo, swHi, shLo, shHi, lhv;
    uint32_t lwv;

    seed(0x1111, 0x2222);
    DEV4 = dev4;
    *(volatile uint32_t *)ADSR = 0xaaaa5555;
    delay();
    DEV4 = SAFE;
    swLo = ADSR[0];
    swHi = ADSR[1];

    seed(0x3333, 0x4444);
    DEV4 = dev4;
    ADSR[0] = 0xbeef;
    delay();
    DEV4 = SAFE;
    shLo = ADSR[0];
    shHi = ADSR[1];

    seed(0x5678, 0x9abc);
    DEV4 = dev4;
    lwv = *(volatile uint32_t *)ADSR;
    lhv = ADSR[1];
    DEV4 = old;
    irqRestore(sr);

    ramsyscall_printf("SPU %-6s dev4=%08x sw(aaaa5555 over 2222:1111)=%04x:%04x sh(beef over 4444:3333)=%04x:%04x "
                      "lw(9abc:5678)=%08x lh+2=%04x\n",
                      name, dev4, swHi, swLo, shHi, shLo, lwv, lhv);
}

int main(void) {
    uint32_t dev2 = DEV2, dev4 = DEV4;
    ramsyscall_printf("AUTOINC-START dev2=%08x dev4=%08x\n", dev2, dev4);
    for (int r = 0; r < 3; r++) {
        romArm("bios", dev2);
        romArm("noinc", dev2 & ~0x2000u);
        spuArm("16inc", SAFE);
        spuArm("16no", SAFE & ~0x2000u);
        spuArm("8inc", SAFE & ~0x1000u);
        spuArm("8no", SAFE & ~0x3000u);
    }
    ramsyscall_printf("AUTOINC-DONE\n");
    return 0;
}
