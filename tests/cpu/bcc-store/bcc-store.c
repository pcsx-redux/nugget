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
 * Where a store goes for a given BCC (FFFE0130h) value and COP0 SR.IsC.
 *
 * Each arm writes 11223344h to a word in this program's .bss through the
 * uncached mirror, runs one access from uncached code with BCC and SR set
 * (probe.s, bcc_op), then reads the word back uncached and reads the
 * i-cache tag of its line in tag test mode (BCC 0001E90Ch, IsC). Before
 * an arm the line's tag is cleared to valid 0 with the target's page, and
 * after it again, so no code runs from a line an arm left valid. The store
 * value ends in 5h, so a tag write shows as valid bits 5h.
 *
 * Nothing runs from .bss, so a tag or code word left at the target's line
 * index only matters for the target's own page.
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

struct BccOp {
    uint32_t biu, sr, addr, value, kind;
};
uint32_t bcc_op(const struct BccOp *op);

enum { SW = 0, SH = 1, SB = 2, LW = 3 };
static const char *const s_kind[] = {"sw", "sh", "sb", "lw"};

#define ISC 0x00010000u
#define BIU_DEFAULT 0x0001e988u
#define BIU_TAG_DS 0x0001e98cu
#define BIU_TAG 0x0001e90cu
#define BIU_NODS 0x0001e908u

static volatile uint32_t s_buf[64] __attribute__((aligned(16)));

#define K1(a) (((uint32_t)(a) & 0x1fffffffu) | 0xa0000000u)

struct Arm {
    const char *name;
    uint32_t biu, sr;
    int kseg1;
    uint32_t kind;
};

static const struct Arm s_arms[] = {
    {"default-isc0-sw", BIU_DEFAULT, 0, 0, SW},
    {"tagds-isc0-sw", BIU_TAG_DS, 0, 0, SW},
    {"tagds-isc0-sh", BIU_TAG_DS, 0, 0, SH},
    {"tagds-isc0-sb", BIU_TAG_DS, 0, 0, SB},
    {"tagds-isc0-sw-k1", BIU_TAG_DS, 0, 1, SW},
    {"tag-isc0-sw", BIU_TAG, 0, 0, SW},
    {"nods-isc0-sw", BIU_NODS, 0, 0, SW},
    {"tag-isc1-sw", BIU_TAG, ISC, 0, SW},
    {"tag-isc1-sw-k1", BIU_TAG, ISC, 1, SW},
    {"tagds-isc1-sw", BIU_TAG_DS, ISC, 0, SW},
    {"tagds-isc1-sh", BIU_TAG_DS, ISC, 0, SH},
    {"tagds-isc1-sb", BIU_TAG_DS, ISC, 0, SB},
    {"tagds-isc1-sw-k1", BIU_TAG_DS, ISC, 1, SW},
    {"default-isc1-sw", BIU_DEFAULT, ISC, 0, SW},
};
#define NARMS (sizeof(s_arms) / sizeof(s_arms[0]))

static uint32_t tagWrite(uint32_t addr, uint32_t value) {
    struct BccOp op = {BIU_TAG, ISC, addr, value, SW};
    return bcc_op(&op);
}

static uint32_t tagRead(uint32_t addr) {
    struct BccOp op = {BIU_TAG, ISC, addr, 0, LW};
    return bcc_op(&op);
}

static uint32_t s_ram[3][NARMS], s_tag[3][NARMS];

int main(void) {
    uint32_t target = (uint32_t)&s_buf[32];
    volatile uint32_t *ram = (volatile uint32_t *)K1(target);
    ramsyscall_printf("BCCSTORE-START bcc=%08x target=%08x\n", *(volatile uint32_t *)0xfffe0130, target);
    for (unsigned pass = 0; pass < 3; pass++) {
        for (unsigned i = 0; i < NARMS; i++) {
            const struct Arm *a = &s_arms[i];
            struct BccOp op = {a->biu, a->sr, a->kseg1 ? K1(target) : target, 0xaabbcc05u | (i << 4), a->kind};
            tagWrite(target, 0);
            *ram = 0x11223344u;
            bcc_op(&op);
            s_ram[pass][i] = *ram;
            s_tag[pass][i] = tagRead(target);
            tagWrite(target, 0);
        }
    }
    for (unsigned i = 0; i < NARMS; i++) {
        const struct Arm *a = &s_arms[i];
        int stable = s_ram[0][i] == s_ram[1][i] && s_ram[1][i] == s_ram[2][i] && s_tag[0][i] == s_tag[1][i] &&
                     s_tag[1][i] == s_tag[2][i];
        ramsyscall_printf("ARM %-18s biu=%08x sr=%08x %s %s value=%08x ram=%08x ramHit=%d tag=%08x tagValid=%x%s\n",
                          a->name, a->biu, a->sr, s_kind[a->kind], a->kseg1 ? "kseg1" : "kseg0",
                          0xaabbcc05u | (i << 4), s_ram[0][i], s_ram[0][i] != 0x11223344u, s_tag[0][i],
                          s_tag[0][i] & 0xf, stable ? "" : " UNSTABLE");
    }
    ramsyscall_printf("BCCSTORE-DONE\n");
    while (1) __asm__ __volatile__("");
}
