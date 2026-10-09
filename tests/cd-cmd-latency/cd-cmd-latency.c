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
 * CD command pickup latency, per console.
 *
 * Write a command to 1F801801h (index 0), then poll status (1F801800h) and
 * the cause bits (1F801803h, index 1) with root counter 2 on sysclk/8, IRQs
 * masked. Two times per sample, both from the command write: BUSYSTS (status
 * bit 7) falling, and a nonzero cause appearing. Commands: GetStat (01h,
 * which is also libcd's CdlNop) and Test 20h (19h with one parameter). Each
 * is taken in three drive states: as booted, after Stop (08h) and after
 * Standby (07h); the stat byte of every GetStat is logged, so whether the
 * motor actually ran (stat bit 1) is in the output rather than assumed.
 * Samples are spaced by a pseudo-random 0..2 ms so the spacing cannot lock
 * to the controller's main-loop period. Probe only: prints timings, no
 * verdict, exits 0.
 */

#include <stdint.h>

#include "common/hardware/cdrom.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define T2_VALUE (*(volatile uint32_t *)0xbf801120)
#define T2_MODE (*(volatile uint32_t *)0xbf801124)

#define NSAMPLES 128
#define TIMEOUT_TICKS 200000u  // ~47 ms at sysclk/8

static uint32_t s_ext, s_last;

// Extended sysclk/8 count. Called often enough (every poll) that the 16-bit
// counter cannot wrap twice between calls.
static inline uint32_t now(void) {
    uint32_t v = T2_VALUE & 0xffff;
    if (v < s_last) s_ext += 0x10000;
    s_last = v;
    return s_ext + v;
}

static void waitTicks(uint32_t ticks) {
    uint32_t t0 = now();
    while (now() - t0 < ticks);
}

static uint32_t s_lfsr = 0xace1u;
static uint32_t rnd(void) {
    s_lfsr = (s_lfsr >> 1) ^ (-(s_lfsr & 1u) & 0xb400u);
    return s_lfsr;
}

static uint8_t s_resp[16];
static unsigned s_respLen;

static void drainResponse(void) {
    s_respLen = 0;
    while (CDROM_REG0_UC & 0x20) {
        uint8_t b = CDROM_REG1_UC;
        if (s_respLen < sizeof(s_resp)) s_resp[s_respLen++] = b;
    }
}

static void ackAll(void) {
    CDROM_REG0_UC = 1;
    CDROM_REG3_UC = 0x1f;
}

// Waits up to timeout for any cause; returns it (0 on timeout), response
// drained, cause acknowledged.
static uint8_t waitCause(uint32_t timeout) {
    uint32_t t0 = now();
    CDROM_REG0_UC = 1;
    uint8_t c = 0;
    while (!(c = CDROM_REG3_UC & 7) && now() - t0 < timeout);
    drainResponse();
    ackAll();
    return c;
}

static const char *causeName(unsigned c) {
    static const char *const names[] = {"none", "dataready", "complete", "acknowledge", "end", "error", "cause6", "cause7"};
    return names[c & 7];
}

typedef struct {
    uint32_t busy, cause;
    uint8_t c, stat;
} Sample;

static Sample s_samples[NSAMPLES];

static int one(uint8_t cmd, int withParam, uint8_t param, Sample *out) {
    // Quiet the interface first: no pending cause, empty response FIFO.
    waitTicks(1000 + (rnd() & 0x1fff));
    drainResponse();
    ackAll();
    if (CDROM_REG3_UC & 7) return -1;
    CDROM_REG0_UC = 0;
    if (withParam) CDROM_REG2_UC = param;
    uint32_t t0 = now();
    CDROM_REG1_UC = cmd;
    CDROM_REG0_UC = 1;
    uint32_t tb = 0, tc = 0;
    uint8_t c = 0;
    for (;;) {
        uint8_t s = CDROM_REG0_UC;
        uint8_t k = CDROM_REG3_UC & 7;
        uint32_t t = now() - t0;
        if (!tb && !(s & 0x80)) tb = t ? t : 1;
        if (!tc && k) {
            tc = t ? t : 1;
            c = k;
        }
        if (tb && tc) break;
        if (t > TIMEOUT_TICKS) break;
    }
    drainResponse();
    ackAll();
    out->busy = tb;
    out->cause = tc;
    out->c = c;
    out->stat = s_respLen ? s_resp[0] : 0xff;
    return (tb && tc) ? 0 : -2;
}

// Ticks of sysclk/8 to tenths of a microsecond: 8 / 33.8688 MHz = 0.2362 us.
static unsigned tenthsUs(uint32_t ticks) { return (ticks * 23620u + 5000u) / 10000u; }

static void insertionSort(uint32_t *v, unsigned n) {
    for (unsigned i = 1; i < n; i++) {
        uint32_t x = v[i];
        unsigned j = i;
        while (j && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
}

static uint32_t s_sorted[NSAMPLES];

static void summary(const char *what, int which, unsigned n) {
    for (unsigned i = 0; i < n; i++) s_sorted[i] = which ? s_samples[i].cause : s_samples[i].busy;
    insertionSort(s_sorted, n);
    const unsigned q[] = {0, n / 10, n / 4, n / 2, (3 * n) / 4, (9 * n) / 10, n - 1};
    ramsyscall_printf("  %s us (min p10 p25 p50 p75 p90 max):", what);
    for (unsigned i = 0; i < 7; i++) {
        unsigned v = tenthsUs(s_sorted[q[i]]);
        ramsyscall_printf(" %d.%d", v / 10, v % 10);
    }
    ramsyscall_printf("\n");
}

static void arm(const char *state, const char *name, uint8_t cmd, int withParam, uint8_t param) {
    unsigned n = 0, errs = 0, causeHist[8] = {0}, motorOn = 0, statKnown = 0;
    for (unsigned i = 0; i < NSAMPLES; i++) {
        Sample s;
        int r = one(cmd, withParam, param, &s);
        if (r) {
            errs++;
            continue;
        }
        s_samples[n++] = s;
        causeHist[s.c]++;
        if (cmd == 0x01 && s.stat != 0xff) {
            statKnown++;
            if (s.stat & 2) motorOn++;
        }
    }
    ramsyscall_printf("ARM state=%s cmd=%s n=%d errs=%d causes=", state, name, n, errs);
    for (unsigned c = 1; c < 8; c++)
        if (causeHist[c]) ramsyscall_printf("%s:%d ", causeName(c), causeHist[c]);
    if (statKnown) ramsyscall_printf("motorOn=%d/%d lastStat=%02x", motorOn, statKnown, s_samples[n ? n - 1 : 0].stat);
    ramsyscall_printf("\n");
    if (!n) return;
    summary("busy ", 0, n);
    summary("cause", 1, n);
    ramsyscall_printf("  raw busy/cause ticks:");
    for (unsigned i = 0; i < n; i++) {
        if ((i & 7) == 0) ramsyscall_printf("\n  ");
        ramsyscall_printf(" %d/%d", (int)s_samples[i].busy, (int)s_samples[i].cause);
    }
    ramsyscall_printf("\n");
}

// Issue a command with a second response (Stop, Standby) and log both.
// Returns nonzero when both responses arrived as acknowledge then complete.
static int driveTo(const char *name, uint8_t cmd) {
    drainResponse();
    ackAll();
    CDROM_REG0_UC = 0;
    CDROM_REG1_UC = cmd;
    uint8_t c1 = waitCause(TIMEOUT_TICKS);
    uint8_t s1 = s_respLen ? s_resp[0] : 0xff;
    uint8_t c2 = waitCause(4233600u * 3u);  // up to 3 s for the second response
    uint8_t s2 = s_respLen ? s_resp[0] : 0xff;
    ramsyscall_printf("DRIVE %s: first %s stat=%02x, second %s stat=%02x\n", name, causeName(c1), s1, causeName(c2), s2);
    waitTicks(423360u);  // 100 ms
    return c1 == 3 && c2 == 2;
}

static void states(const char *state) {
    arm(state, "GetStat", 0x01, 0, 0);
    arm(state, "Test20", 0x19, 1, 0x20);
}

int main(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    uint32_t off = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(off));
    T2_MODE = 0x0200;  // sysclk/8, free-running, no IRQ
    s_last = T2_VALUE & 0xffff;
    CDROM_REG0_UC = 1;
    CDROM_REG2_UC = 0x1f;  // enable every cause
    ramsyscall_printf("CDLAT-START status=%02x\n", CDROM_REG0_UC);
    states("booted");
    // A refused transition (no disc, lid open) still gets measured, under a
    // label that says the command was refused.
    states(driveTo("Stop", 0x08) ? "stopped" : "stop-refused");
    states(driveTo("Standby", 0x07) ? "standby" : "standby-refused");
    ramsyscall_printf("CDLAT-DONE\n");
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
    return 0;
}
