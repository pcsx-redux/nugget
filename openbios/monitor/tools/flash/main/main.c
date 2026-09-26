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


/* H2700 flash programmer, run as a target under the resident monitor from
   main RAM. Mirrors pflash: JEDEC/AMD byte commands through the 0x5555/0x2AAA
   aperture of the BIOS ROM window, window widened to 1 MB by writing the
   BIOS_ROM delay/size register, chip erase then byte program with data polling.
   Built twice: DRY (identify, mirror test, diff against the payload, no writes)
   and GO (DRY checks, then erase, program, verify). Exit code 0 = clean. */
#include <stdint.h>
#include "common/syscalls/syscalls.h"

extern const uint8_t g_payload[];
extern const uint8_t g_payload_end[];

#define FLASH ((volatile uint8_t *)0xbfc00000u)
#define BIOS_ROM_CTRL (*(volatile uint32_t *)0x1f801010u)
#define IMAGE_SIZE 0x80000u

static char *puthex(char *p, uint32_t v, int d) {
    static const char h[] = "0123456789ABCDEF";
    for (int i = (d - 1) * 4; i >= 0; i -= 4) *p++ = h[(v >> i) & 15];
    return p;
}
static char *putdec(char *p, uint32_t v) {
    char t[12]; int i = 0;
    do { t[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *p++ = t[--i];
    return p;
}
static char *putstr(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static void say(const char *s) { syscall_puts(s); }
static void sayhex(const char *k, uint32_t v) { char b[64], *p = b; p = putstr(p, k); p = puthex(p, v, 8); *p++ = '\n'; *p = 0; say(b); }
static void saydec(const char *k, uint32_t v) { char b[64], *p = b; p = putstr(p, k); p = putdec(p, v); *p++ = '\n'; *p = 0; say(b); }

static void unlock(void) { FLASH[0x5555] = 0xAA; FLASH[0x2AAA] = 0x55; }
static void reset_read(void) { FLASH[0x5555] = 0xF0; }

static void do_exit(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    for (;;) {}
}

int main(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12" : : "r"(sr & ~1u));

    say("=== H2700 FLASH " MODE " ===\n");
    saydec("payload bytes: ", (uint32_t)(g_payload_end - g_payload));
    if ((uint32_t)(g_payload_end - g_payload) != IMAGE_SIZE) { say("bad payload size\n"); do_exit(2); }

    uint32_t oldbus = BIOS_ROM_CTRL;
    sayhex("BIOS_ROM ctrl as found: ", oldbus);
    BIOS_ROM_CTRL = 0x142433u; /* pflash SetRomBus: 1 MB window */

    /* Mirror test, as pflash's detectFlashBase_: does +0x20000 alias +0? */
    int alias20 = 1, alias80 = 1;
    for (int i = 0; i < 32; i++) { if (FLASH[i] != FLASH[0x20000 + i]) alias20 = 0; if (FLASH[i] != FLASH[0x80000 + i]) alias80 = 0; }
    saydec("mirror +0x20000: ", alias20);
    saydec("mirror +0x80000: ", alias80);

    /* JEDEC autoselect: manufacturer at 0, device at 1. */
    unlock(); FLASH[0x5555] = 0x90;
    uint32_t id0 = FLASH[0], id1 = FLASH[1], id2 = FLASH[2];
    reset_read();
    sayhex("id[0] (mfr): ", id0);
    sayhex("id[1] (dev): ", id1);
    sayhex("id[2]      : ", id2);
    sayhex("flash[0..3] after reset: ", FLASH[0] | (FLASH[1] << 8) | (FLASH[2] << 16) | (FLASH[3] << 24));

    uint32_t diff = 0, cave_nonff = 0;
    for (uint32_t i = 0; i < IMAGE_SIZE; i++) {
        if (FLASH[i] != g_payload[i]) diff++;
        if (i >= 0x40000 && i < 0x60000 && FLASH[i] != 0xFF) cave_nonff++;
    }
    saydec("bytes differing from payload: ", diff);
    saydec("cave bytes not 0xFF now: ", cave_nonff);

#ifdef FLASH_GO
    say("chip erase...\n");
    unlock(); FLASH[0x5555] = 0x80; unlock(); FLASH[0x5555] = 0x10;
    uint32_t spins = 0;
    while (FLASH[0] != 0xFF) { if (++spins > 200000000u) { say("erase timeout\n"); BIOS_ROM_CTRL = oldbus; do_exit(3); } }
    uint32_t left = 0;
    for (uint32_t i = 0; i < IMAGE_SIZE; i++) if (FLASH[i] != 0xFF) left++;
    saydec("erase done, non-FF bytes left: ", left);
    if (left) { BIOS_ROM_CTRL = oldbus; do_exit(4); }

    say("programming...\n");
    for (uint32_t i = 0; i < IMAGE_SIZE; i++) {
        uint8_t d = g_payload[i];
        if (d == 0xFF) continue;
        unlock(); FLASH[0x5555] = 0xA0; FLASH[i] = d;
        spins = 0;
        while (FLASH[i] != d) { if (++spins > 2000000u) { sayhex("program timeout at ", i); BIOS_ROM_CTRL = oldbus; do_exit(5); } }
        if ((i & 0xFFFF) == 0) sayhex("  at ", i);
    }
    uint32_t bad = 0, firstbad = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < IMAGE_SIZE; i++) if (FLASH[i] != g_payload[i]) { if (!bad) firstbad = i; bad++; }
    saydec("verify mismatches: ", bad);
    if (bad) sayhex("first mismatch at ", firstbad);
    BIOS_ROM_CTRL = oldbus;
    do_exit(bad ? 6 : 0);
#else
    BIOS_ROM_CTRL = oldbus;
    do_exit(0);
#endif
    return 0;
}
