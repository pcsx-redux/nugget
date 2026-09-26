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

/* Flash cart programmer, run as a target under the monitor. For the AMD
   Am29F010 (128 KB, JEDEC 01h/20h) on the farm's Power Replay carts at EXP1:
   byte commands through the 0x5555/0x2AAA aperture, chip erase, then the
   unlock/A0 sequence before every byte with data polling, as Unirom does for
   this chip. Built twice: DRY (identify and diff against the payload, no
   writes) and GO (DRY checks, then erase, program, verify; skipped when the
   cart already holds the payload). The image is padded with 0xFF to the chip
   size. Exit code 0 = clean. */
#include <stdint.h>

#include "common/syscalls/syscalls.h"

extern const uint8_t g_payload[];
extern const uint8_t g_payload_end[];

#define CART ((volatile uint8_t *)0x1f000000u)
#define CHIP_SIZE 0x20000u
#define MFR_AMD 0x01
#define DEV_AM29F010 0x20

static char *puthex(char *p, uint32_t v, int d) {
    static const char h[] = "0123456789ABCDEF";
    for (int i = (d - 1) * 4; i >= 0; i -= 4) *p++ = h[(v >> i) & 15];
    return p;
}
static char *putdec(char *p, uint32_t v) {
    char t[12];
    int i = 0;
    do {
        t[i++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (i) *p++ = t[--i];
    return p;
}
static char *putstr(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}
static void say(const char *s) { syscall_puts(s); }
static void sayhex(const char *k, uint32_t v) {
    char b[64], *p = b;
    p = putstr(p, k);
    p = puthex(p, v, 8);
    *p++ = '\n';
    *p = 0;
    say(b);
}
static void saydec(const char *k, uint32_t v) {
    char b[64], *p = b;
    p = putstr(p, k);
    p = putdec(p, v);
    *p++ = '\n';
    *p = 0;
    say(b);
}

static void unlock(void) {
    CART[0x5555] = 0xAA;
    CART[0x2AAA] = 0x55;
}
static void resetRead(void) {
    unlock();
    CART[0x5555] = 0xF0;
}
static void pause(int n) {
    for (volatile int i = 0; i < n; i++) {
    }
}

static __attribute__((noreturn)) void exitWith(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    __builtin_unreachable();
}

static uint8_t want(uint32_t i, uint32_t n) { return i < n ? g_payload[i] : 0xFF; }

int main(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12" : : "r"(sr & ~1u));

    uint32_t n = (uint32_t)(g_payload_end - g_payload);
    say("=== CART FLASH " MODE " ===\n");
    saydec("payload bytes: ", n);
    if (n == 0 || n > CHIP_SIZE) {
        say("bad payload size\n");
        exitWith(2);
    }

    /* Unirom pauses around the ID read on this chip; reading straight after
       the 0x90 returns array data (measured: 20h/03h, the cart's first
       bytes). */
    unlock();
    CART[0x5555] = 0x90;
    pause(100000);
    uint32_t mfr = CART[0], dev = CART[1];
    pause(100000);
    resetRead();
    pause(100000);
    sayhex("id[0] (mfr): ", mfr);
    sayhex("id[1] (dev): ", dev);
    if (mfr != MFR_AMD || dev != DEV_AM29F010) {
        say("not an Am29F010, stopping\n");
        exitWith(7);
    }

    uint32_t diff = 0, firstDiff = 0xffffffffu;
    for (uint32_t i = 0; i < CHIP_SIZE; i++) {
        if (CART[i] != want(i, n)) {
            if (!diff) firstDiff = i;
            diff++;
        }
    }
    saydec("bytes differing from payload: ", diff);
    if (diff) sayhex("first difference at ", firstDiff);

#ifdef FLASH_GO
    /* Every erase cycle wears the chip; skip it when there is nothing to do. */
    if (diff == 0) {
        say("cart already matches the payload, not erasing\n");
        exitWith(0);
    }
    say("chip erase...\n");
    unlock();
    CART[0x5555] = 0x80;
    unlock();
    CART[0x5555] = 0x10;
    uint32_t spins = 0;
    while (CART[0] != 0xFF) {
        if (++spins > 200000000u) {
            say("erase timeout\n");
            resetRead();
            exitWith(3);
        }
    }
    uint32_t left = 0;
    for (uint32_t i = 0; i < CHIP_SIZE; i++) {
        if (CART[i] != 0xFF) left++;
    }
    saydec("erase done, non-FF bytes left: ", left);
    if (left) exitWith(4);

    say("programming...\n");
    for (uint32_t i = 0; i < n; i++) {
        uint8_t d = g_payload[i];
        if (d == 0xFF) continue;
        unlock();
        CART[0x5555] = 0xA0;
        pause(10);
        CART[i] = d;
        spins = 0;
        while (CART[i] != d) {
            if (++spins > 2000000u) {
                sayhex("program timeout at ", i);
                resetRead();
                exitWith(5);
            }
        }
    }
    uint32_t bad = 0, firstBad = 0xffffffffu;
    for (uint32_t i = 0; i < CHIP_SIZE; i++) {
        if (CART[i] != want(i, n)) {
            if (!bad) firstBad = i;
            bad++;
        }
    }
    saydec("verify mismatches: ", bad);
    if (bad) sayhex("first mismatch at ", firstBad);
    exitWith(bad ? 6 : 0);
#else
    exitWith(0);
#endif
    return 0;
}
