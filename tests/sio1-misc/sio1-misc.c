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
 * SIO1 1F80105Ch, every access width and alignment in 5Ch-5Fh, graded
 * against the silicon results in RESULTS.md.
 *
 * Each case sets 5Ch and 5Eh (the baud reload) to known values, selects a
 * character length, does one store, then reads 5Ch-5Fh back with lbu, lhu
 * and lw. The store's source register holds a different byte in every lane,
 * so a store that ignores its width shows up as the other lanes landing.
 *
 * The model: a store drives the bus with the source shifted left by eight
 * times the low two address bits, and the halfword register at (address &
 * ~1) latches its own 16 bits of the bus, whatever the access width. 5Ch
 * stores the halfword byte-swapped, each byte trimmed to the character
 * length (5 bits: (b & 1Fh) | C0h, 6 bits: (b & 3Fh) | 80h, 7 bits: b & 7Fh);
 * 5Eh stores it as is. A 32-bit store at 5Ch does not touch 5Eh. Reads return
 * 5Eh:5Ch as one word.
 *
 * Farm consoles run their monitor over SIO1, so nothing prints until mode,
 * baud and 5Ch are restored.
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

#define B 0xbf801050
#define R8(a) (*(volatile uint8_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))

/* The source register must reach the bus whole, so the stores are asm. */
static inline void sb(uint32_t a, uint32_t v) { __asm__ volatile("sb %0, 0(%1)" ::"r"(v), "r"(a) : "memory"); }
static inline void sh(uint32_t a, uint32_t v) { __asm__ volatile("sh %0, 0(%1)" ::"r"(v), "r"(a) : "memory"); }
static inline void sw(uint32_t a, uint32_t v) { __asm__ volatile("sw %0, 0(%1)" ::"r"(v), "r"(a) : "memory"); }

struct Arm {
    const char *name;
    int width, off;
};
static const struct Arm arms[] = {
    {"sb5C", 1, 0}, {"sb5D", 1, 1}, {"sb5E", 1, 2}, {"sb5F", 1, 3}, {"sh5C", 2, 0}, {"sh5E", 2, 2}, {"sw5C", 4, 0},
};
#define NARMS (sizeof(arms) / sizeof(arms[0]))
static const uint32_t srcs[] = {0xa5c35a3c, 0x3ca5c35a, 0x5a3ca5c3, 0xc35a3ca5};
#define NSRC (sizeof(srcs) / sizeof(srcs[0]))
#define NCL 4

/* 6996h written at 8 bits reads back 9669h. */
#define BASE_IN 0x6996
#define BASE_5C 0x9669
#define BASE_5E 0x1234

struct Rec {
    uint16_t h5c, h5e, mr;
    uint32_t w;
    uint8_t b[4];
};
static struct Rec recs[NCL][NARMS][NSRC];
static uint16_t base5c[NCL];

static uint8_t trim(uint8_t b, int cl) {
    switch (cl) {
        case 0:
            return (b & 0x1f) | 0xc0;
        case 1:
            return (b & 0x3f) | 0x80;
        case 2:
            return b & 0x7f;
    }
    return b;
}

static void expect(const struct Arm *a, uint32_t src, int cl, uint16_t *e5c, uint16_t *e5e) {
    uint32_t bus = src << (a->off * 8);
    uint16_t h = bus >> ((a->off & 2) * 8);
    *e5c = BASE_5C;
    *e5e = BASE_5E;
    if (a->off & 2) {
        *e5e = h;
    } else {
        *e5c = (uint16_t)(trim(h & 0xff, cl) << 8) | trim(h >> 8, cl);
    }
}

static void store(const struct Arm *a, uint32_t v) {
    uint32_t addr = B + 12 + a->off;
    if (a->width == 1)
        sb(addr, v);
    else if (a->width == 2)
        sh(addr, v);
    else
        sw(addr, v);
}

static uint16_t modeFor(uint16_t mode, int cl) { return (mode & ~0xc) | (cl << 2); }

int main(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12; nop; nop" ::"r"(sr & ~1u));
    /* let the monitor's last byte leave before the mode changes */
    for (int i = 0; i < 1000000 && !(R32(B + 4) & 4); i++);
    uint16_t smode = R16(B + 8), sbaud = R16(B + 14), smisc = R16(B + 12);
    for (int cl = 0; cl < NCL; cl++) {
        for (unsigned a = 0; a < NARMS; a++) {
            for (unsigned s = 0; s < NSRC; s++) {
                R16(B + 8) = modeFor(smode, 3);
                R16(B + 12) = BASE_IN;
                R16(B + 14) = BASE_5E;
                R16(B + 8) = modeFor(smode, cl);
                if (a == 0 && s == 0) base5c[cl] = R16(B + 12);
                store(&arms[a], srcs[s]);
                struct Rec *r = &recs[cl][a][s];
                r->h5c = R16(B + 12);
                r->h5e = R16(B + 14);
                r->w = R32(B + 12);
                for (int k = 0; k < 4; k++) r->b[k] = R8(B + 12 + k);
                r->mr = R16(B + 8);
            }
        }
    }
    /* 5Ch swaps on the way in, so write the saved value pre-swapped */
    R16(B + 8) = modeFor(smode, 3);
    R16(B + 12) = (uint16_t)((smisc << 8) | (smisc >> 8));
    R16(B + 8) = smode;
    R16(B + 14) = sbaud;
    __asm__ volatile("mtc0 %0, $12; nop; nop" ::"r"(sr));

    int pass = 0, fail = 0;
    for (int cl = 0; cl < NCL; cl++) {
        if (base5c[cl] != BASE_5C) {
            ramsyscall_printf("SIO1MISC FAIL %d bits: baseline 5C=%04x after the mode change, expected %04x\n", cl + 5,
                              base5c[cl], BASE_5C);
            fail++;
        }
        for (unsigned a = 0; a < NARMS; a++) {
            for (unsigned s = 0; s < NSRC; s++) {
                struct Rec *r = &recs[cl][a][s];
                uint16_t e5c, e5e;
                expect(&arms[a], srcs[s], cl, &e5c, &e5e);
                uint32_t ew = ((uint32_t)e5e << 16) | e5c;
                int ok = r->h5c == e5c && r->h5e == e5e && r->w == ew && r->mr == modeFor(smode, cl);
                for (int k = 0; k < 4; k++) ok = ok && r->b[k] == (uint8_t)(ew >> (k * 8));
                if (ok) {
                    pass++;
                    ramsyscall_printf("SIO1MISC ok   %d bits %s src=%08x 5C=%04x 5E=%04x\n", cl + 5, arms[a].name,
                                      srcs[s], r->h5c, r->h5e);
                } else {
                    fail++;
                    ramsyscall_printf(
                        "SIO1MISC FAIL %d bits %s src=%08x lhu=%04x/%04x lw=%08x lbu=%02x %02x %02x %02x mode=%04x, "
                        "expected 5C=%04x 5E=%04x mode=%04x\n",
                        cl + 5, arms[a].name, srcs[s], r->h5c, r->h5e, r->w, r->b[0], r->b[1], r->b[2], r->b[3], r->mr,
                        e5c, e5e, modeFor(smode, cl));
                }
            }
        }
    }
    if (R16(B + 8) != smode || R16(B + 14) != sbaud || R16(B + 12) != smisc) {
        ramsyscall_printf("SIO1MISC FAIL restore: mode=%04x baud=%04x 5C=%04x, saved %04x %04x %04x\n", R16(B + 8),
                          R16(B + 14), R16(B + 12), smode, sbaud, smisc);
        fail++;
    }
    int ok = fail == 0 && pass == NCL * NARMS * NSRC;
    if (ok)
        ramsyscall_printf("SIO1MISC-PASS %d cases\n", pass);
    else
        ramsyscall_printf("SIO1MISC-FAIL %d passed, %d failed\n", pass, fail);
    return ok ? 0 : 1;
}
