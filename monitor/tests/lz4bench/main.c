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

/* Times lz4StreamFeed on the PS1: each payload stream is decoded from RAM one
   byte at a time with interrupts off, and counter 2 (system clock) is read
   around every byte. A control pass runs the same loop without decoding, so
   its per-byte cost can be subtracted. Exits 0 when every stream decodes to
   the expected length and sum. */

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/syscalls/syscalls.h"
#include "monitor/lz4stream.h"

extern const uint8_t g_payload[];

static uint8_t s_out[160 * 1024];

static __attribute__((noreturn)) void exitWith(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    __builtin_unreachable();
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

struct Result {
    uint32_t in, cycles, max;
};

static const uint8_t *run(const uint8_t *p, uint32_t nblocks, int decode, struct Result *r, int *err) {
    struct Lz4Stream s;
    lz4StreamInit(&s, s_out);
    uint32_t in = 0, cycles = 0, max = 0;
    uint16_t prev = COUNTERS[2].value;
    for (uint32_t b = 0; b < nblocks; b++) {
        uint32_t len = p[0] | (p[1] << 8);
        p += 2;
        for (uint32_t i = 0; i < len; i++) {
            if (decode) *err |= lz4StreamFeed(&s, p[i]);
            uint16_t now = COUNTERS[2].value;
            uint16_t d = now - prev;
            prev = now;
            cycles += d;
            if (d > max) max = d;
        }
        if (decode) *err |= lz4StreamEndBlock(&s);
        in += len;
        p += len;
    }
    r->in = in;
    r->cycles = cycles;
    r->max = max;
    return p;
}

int main(void) {
    const uint8_t *p = g_payload;
    uint32_t streams = rd32(p);
    p += 4;
    int bad = 0;
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12" : : "r"(sr & ~1u));
    COUNTERS[2].mode = 0;
    for (uint32_t k = 0; k < streams; k++) {
        uint32_t raw = rd32(p), sum = rd32(p + 4), nblocks = rd32(p + 8);
        p += 12;
        struct Result ctl, dec;
        int err = 0;
        run(p, nblocks, 0, &ctl, &err);
        const uint8_t *next = run(p, nblocks, 1, &dec, &err);
        uint32_t got = 0;
        for (uint32_t i = 0; i < raw; i++) got += s_out[i];
        int ok = !err && got == sum;
        bad |= !ok;
        uint32_t net = dec.cycles - ctl.cycles;
        ramsyscall_printf("lz4bench: stream %d %s raw %d in %d cycles %d control %d net %d max-byte %d control-max %d\n", k,
                          ok ? "OK" : "BAD", raw, dec.in, dec.cycles, ctl.cycles, net, dec.max, ctl.max);
        p = next;
        while ((uintptr_t)p & 3) p++;
    }
    __asm__ volatile("mtc0 %0, $12" : : "r"(sr));
    exitWith(bad ? 0xbad : 0);
}
