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
 * FIFO depths: GP0 write FIFO, GPUREAD, MDEC data-in, MDEC data-out.
 *
 * GP0: a 512x256 VRAM-to-VRAM copy keeps the GPU busy. Behind it, a GP0(A0h)
 *   header and NW distinct data words are written by CPU, each write timed on
 *   root counter 2 (sysclk/8) together with a GPUSTAT read. After the copy,
 *   the rest of the upload is written with different marker words and the
 *   row is read back. A full FIFO either stalls a write (a long dt at that
 *   index) or drops it (the readback shows a gap). Idle arm = same with no
 *   copy, all dt short, all words in place.
 * GPUREAD: GP0(C0h) on a patterned row, j words read, then a GP0(02h) fill
 *   over the same row is written; if the GPU runs the fill while the read is
 *   pending, words already fetched into the read FIFO stay old.
 * MDEC in: MDEC(1) with a large word count and the output never drained;
 *   block words written by CPU, status read after each. Status low 16 bits
 *   (remaining-1) show how many the decoder took.
 * MDEC out: MDEC(1) of k mono blocks, not drained, then status and a count
 *   of words readable until bit 31.
 */

#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define T2_VALUE (*(volatile uint32_t *)0xbf801120)
#define T2_MODE (*(volatile uint32_t *)0xbf801124)
#define MDEC0 HW_U32(0x1f801820)
#define MDEC1 HW_U32(0x1f801824)

#define ST_NOT_FULL (1u << 25)
#define ST_READY (1u << 26)
#define ST_READ_RDY (1u << 27)
#define ST_EMPTY (1u << 28)

#define REPS 3

static inline uint32_t now(void) { return T2_VALUE & 0xffff; }
static inline uint32_t since(uint32_t t0) { return (now() - t0) & 0xffff; }

static void irqOff(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr &= ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

static void delay(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++);
}

/* ------------------------------------------------------------------ GPU */

static void fullReset(void) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x03000001;
    GPU_STATUS = 0x04000001; /* bit 25 = FIFO not full */
}

static int waitIdle(void) {
    for (int i = 0; i < 2000000; i++) {
        uint32_t s = GPU_STATUS;
        if ((s & ST_READY) && (s & ST_EMPTY)) return 1;
    }
    return 0;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
    waitIdle();
    GPU_DATA = 0x02000000 | c;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (h << 16) | w;
    waitIdle();
}

/* Returns the number of words for which bit 27 never came. */
static int readRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t *out, int n) {
    int to = 0;
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (h << 16) | w;
    for (int k = 0; k < n; k++) {
        int i = 0;
        while (!(GPU_STATUS & ST_READ_RDY) && i < 100000) i++;
        if (i >= 100000) to++;
        out[k] = GPU_DATA;
    }
    waitIdle();
    return to;
}

static void startCopy(void) {
    GPU_DATA = 0x80000000;
    GPU_DATA = (0 << 16) | 0;
    GPU_DATA = (256 << 16) | 0;
    GPU_DATA = (256 << 16) | 512;
}

static void copyProof(int rep) {
    uint32_t px[2];
    fullReset();
    fill(0, 0, 512, 256, 0x0000f8);
    fill(0, 256, 512, 256, 0xf80000);
    readRect(300, 400, 2, 1, px, 1);
    uint32_t before = px[0];
    waitIdle();
    uint32_t t0 = now();
    startCopy();
    uint32_t polls = 0;
    while (!(GPU_STATUS & ST_READY) && polls < 2000000) polls++;
    uint32_t dt = since(t0);
    waitIdle();
    readRect(300, 400, 2, 1, px, 1);
    uint32_t after = px[0];
    readRect(300, 144, 2, 1, px, 1);
    ramsyscall_printf("OBS copy rep=%d ticks8=%u polls=%u dst %08x->%08x src=%08x %s\n", rep, dt, polls, before,
                      after, px[0], (after != before && after == px[0]) ? "COPIED" : "NOT-COPIED");
}

#define GP0_ROWW 128 /* pixels, 64 words */
#define GP0_WORDS 64
static uint32_t s_dt[96], s_st[96], s_rb[128];

static inline uint32_t burstWord(int i) { return ((0x0200u + i) << 16) | (0x0100u + i); }
static inline uint32_t lateWord(int j) { return ((0x0400u + j) << 16) | (0x0300u + j); }

/* Decoded token for a readback word: i for burst word i, 100+j for late word j, -1 other. */
static int token(uint32_t v) {
    uint32_t lo = v & 0xffff, hi = v >> 16;
    if ((lo & 0xff00) == 0x0100 && hi == lo + 0x100) return lo & 0xff;
    if ((lo & 0xff00) == 0x0300 && hi == lo + 0x100) return 100 + (lo & 0xff);
    return -1;
}

static void gp0Arm(const char *tag, int busy, int nw, int rep) {
    fullReset();
    fill(0, 0, 512, 256, 0x0000f8);
    fill(0, 256, 512, 256, 0xf80000);
    fill(640, 300, GP0_ROWW, 1, 0x000000);
    waitIdle();
    uint32_t tc = now();
    if (busy) startCopy();
    uint32_t stHdr0 = GPU_STATUS;
    GPU_DATA = 0xa0000000;
    GPU_DATA = (300 << 16) | 640;
    GPU_DATA = (1 << 16) | GP0_ROWW;
    uint32_t stHdr = GPU_STATUS;
    for (int i = 0; i < nw; i++) {
        uint32_t t0 = now();
        GPU_DATA = burstWord(i);
        uint32_t s = GPU_STATUS;
        s_dt[i] = since(t0);
        s_st[i] = s;
    }
    uint32_t tBurst = since(tc);
    /* the queue drains when the copy ends and the A0h consumes it */
    uint32_t polls = 0;
    while (!(GPU_STATUS & ST_EMPTY) && polls < 2000000) polls++;
    uint32_t tDrain = since(tc);
    for (int j = 0; j < GP0_WORDS - nw; j++) GPU_DATA = lateWord(j);
    int idle = waitIdle();
    int to = readRect(640, 300, GP0_ROWW, 1, s_rb, GP0_WORDS);

    ramsyscall_printf("OBS gp0 %s rep=%d nw=%d stPreHdr=%08x stHdr=%08x tBurst8=%u tDrain8=%u drainPolls=%u idle=%d rbTO=%d\n",
                      tag, rep, nw, stHdr0, stHdr, tBurst, tDrain, polls, idle, to);
    ramsyscall_printf("OBS gp0 %s rep=%d dt8:", tag, rep);
    for (int i = 0; i < nw; i++) ramsyscall_printf(" %u", s_dt[i]);
    ramsyscall_printf("\nOBS gp0 %s rep=%d b25:", tag, rep);
    for (int i = 0; i < nw; i++) ramsyscall_printf("%c", (s_st[i] & ST_NOT_FULL) ? '1' : '0');
    ramsyscall_printf(" b28:");
    for (int i = 0; i < nw; i++) ramsyscall_printf("%c", (s_st[i] & ST_EMPTY) ? '1' : '0');
    ramsyscall_printf(" b26:");
    for (int i = 0; i < nw; i++) ramsyscall_printf("%c", (s_st[i] & ST_READY) ? '1' : '0');
    int inPlace = 0, firstBad = -1;
    for (int k = 0; k < GP0_WORDS; k++) {
        uint32_t want = (k < nw) ? burstWord(k) : lateWord(k - nw);
        if (s_rb[k] == want)
            inPlace++;
        else if (firstBad < 0)
            firstBad = k;
    }
    ramsyscall_printf("\nOBS gp0 %s rep=%d inPlace=%d/%d firstBad=%d rb:", tag, rep, inPlace, GP0_WORDS, firstBad);
    for (int k = 0; k < GP0_WORDS; k++) {
        int t = token(s_rb[k]);
        if (t < 0)
            ramsyscall_printf(" x%08x", s_rb[k]);
        else
            ramsyscall_printf(" %d", t);
    }
    ramsyscall_printf("\n");
}

/* ------------------------------------------------------------- GPUREAD */

#define GR_X 640
#define GR_Y 400
#define GR_W 256 /* pixels, 128 words */
#define GR_WORDS 128
#define FILLWORD 0x001f001fu
static inline uint32_t patWord(int k) { return ((0x5001u + 2 * k) << 16) | (0x5000u + 2 * k); }

static void uploadPattern(void) {
    waitIdle();
    GPU_DATA = 0xa0000000;
    GPU_DATA = (GR_Y << 16) | GR_X;
    GPU_DATA = (1 << 16) | GR_W;
    for (int k = 0; k < GR_WORDS; k++) {
        while (!(GPU_STATUS & ST_NOT_FULL));
        GPU_DATA = patWord(k);
    }
    waitIdle();
}

enum { GR_NOFILL, GR_FILLBEFORE, GR_FILLDURING };

static void grArm(const char *tag, int mode, int j, int rep) {
    fullReset();
    uploadPattern();
    if (mode == GR_FILLBEFORE) fill(GR_X, GR_Y, GR_W, 1, 0x0000f8);
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (GR_Y << 16) | GR_X;
    GPU_DATA = (1 << 16) | GR_W;
    delay(3000);
    uint32_t st0 = GPU_STATUS;
    int to = 0;
    for (int k = 0; k < j; k++) {
        int i = 0;
        while (!(GPU_STATUS & ST_READ_RDY) && i < 100000) i++;
        if (i >= 100000) to++;
        s_rb[k] = GPU_DATA;
    }
    uint32_t st1 = GPU_STATUS;
    if (mode == GR_FILLDURING) {
        GPU_DATA = 0x020000f8;
        GPU_DATA = (GR_Y << 16) | GR_X;
        GPU_DATA = (1 << 16) | GR_W;
        delay(30000);
    }
    uint32_t st2 = GPU_STATUS;
    for (int k = j; k < GR_WORDS; k++) {
        int i = 0;
        while (!(GPU_STATUS & ST_READ_RDY) && i < 100000) i++;
        if (i >= 100000) to++;
        s_rb[k] = GPU_DATA;
    }
    uint32_t st3 = GPU_STATUS;
    int idle = waitIdle();
    uint32_t st4 = GPU_STATUS;
    int old = 0, nw = 0, other = 0, firstNew = -1, lastOld = -1;
    for (int k = 0; k < GR_WORDS; k++) {
        if (s_rb[k] == patWord(k)) {
            old++;
            lastOld = k;
        } else if (s_rb[k] == FILLWORD) {
            nw++;
            if (firstNew < 0) firstNew = k;
        } else
            other++;
    }
    /* Did the fill land in VRAM at all (afterwards)? */
    uint32_t after[2];
    readRect(GR_X + 200, GR_Y, 2, 1, after, 1);
    ramsyscall_printf(
        "OBS gpuread %s rep=%d j=%d old=%d new=%d other=%d firstNew=%d lastOld=%d TO=%d idle=%d st=%08x,%08x,%08x,%08x,%08x vramAfter=%08x\n",
        tag, rep, j, old, nw, other, firstNew, lastOld, to, idle, st0, st1, st2, st3, st4, after[0]);
    if (other || (mode == GR_FILLDURING)) {
        ramsyscall_printf("OBS gpuread %s rep=%d j=%d map:", tag, rep, j);
        for (int k = 0; k < GR_WORDS; k++) {
            if (s_rb[k] == patWord(k))
                ramsyscall_printf("o");
            else if (s_rb[k] == FILLWORD)
                ramsyscall_printf("n");
            else
                ramsyscall_printf("?");
        }
        ramsyscall_printf("\n");
        if (other) {
            ramsyscall_printf("OBS gpuread %s rep=%d j=%d others:", tag, rep, j);
            int c = 0;
            for (int k = 0; k < GR_WORDS && c < 12; k++)
                if (s_rb[k] != patWord(k) && s_rb[k] != FILLWORD) {
                    ramsyscall_printf(" %d=%08x", k, s_rb[k]);
                    c++;
                }
            ramsyscall_printf("\n");
        }
    }
}

/* Per-read timing straight after the C0h header, no bit-27 wait. */
static void grTiming(int rep, int pre) {
    fullReset();
    uploadPattern();
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (GR_Y << 16) | GR_X;
    GPU_DATA = (1 << 16) | GR_W;
    if (pre) delay(3000);
    for (int k = 0; k < 48; k++) {
        uint32_t t0 = now();
        s_rb[k] = GPU_DATA;
        s_st[k] = GPU_STATUS;
        s_dt[k] = since(t0);
    }
    for (int k = 48; k < GR_WORDS; k++) s_rb[k] = GPU_DATA;
    waitIdle();
    int ok = 0;
    for (int k = 0; k < GR_WORDS; k++) ok += s_rb[k] == patWord(k);
    ramsyscall_printf("OBS grtime rep=%d pre=%d ok=%d/%d dt8:", rep, pre, ok, GR_WORDS);
    for (int k = 0; k < 48; k++) ramsyscall_printf(" %u", s_dt[k]);
    ramsyscall_printf(" b27:");
    for (int k = 0; k < 48; k++) ramsyscall_printf("%c", (s_st[k] & ST_READ_RDY) ? '1' : '0');
    ramsyscall_printf("\n");
}

/* ---------------------------------------------------------------- MDEC */

#define M_FULL (1u << 30)
#define M_EMPTY (1u << 31)
#define M_BUSY (1u << 29)
#define BLOCKWORD 0xfe000400u /* DC q=1 dc=0, then end of block */

static int mdecWaitNotBusy(void) {
    for (int i = 0; i < 1000000; i++)
        if (!(MDEC1 & M_BUSY)) return 1;
    return 0;
}

static void mdecInit(void) {
    MDEC1 = 0x80000000;
    MDEC1 = 0;
    MDEC0 = 0x40000000; /* luma quant, 16 words */
    for (int i = 0; i < 16; i++) MDEC0 = 0x01010101;
    int a = mdecWaitNotBusy();
    MDEC0 = 0x60000000; /* scale, 32 words */
    for (int i = 0; i < 32; i++) MDEC0 = 0x5a825a82;
    int b = mdecWaitNotBusy();
    ramsyscall_printf("OBS mdecinit q=%d s=%d st=%08x\n", a, b, MDEC1);
}

static const char *depthName(int d) {
    static const char *n[4] = {"4bit", "8bit", "24bit", "15bit"};
    return n[d & 3];
}

/* Out: k mono blocks (8 words each at 4-bit, 16 at 8-bit), not drained. */
static void mdecOutArm(int depth, int k, int rep) {
    MDEC1 = 0x80000000;
    MDEC1 = 0;
    MDEC0 = 0x20000000 | (depth << 27) | k;
    for (int i = 0; i < k; i++) MDEC0 = BLOCKWORD;
    delay(50000);
    uint32_t s = MDEC1;
    int cnt = 0, gaps = 0;
    for (int n = 0; n < 4096; n++) {
        uint32_t t = MDEC1;
        if (t & M_EMPTY) {
            /* empty: is the decoder still going to refill? */
            int i = 0;
            while ((MDEC1 & M_EMPTY) && i < 20000) i++;
            if (i >= 20000) break;
            gaps++;
        }
        (void)MDEC0;
        cnt++;
    }
    uint32_t e = MDEC1;
    ramsyscall_printf("OBS mdecout %s k=%d rep=%d undrained=%08x busy=%d rem=%04x blk=%d empty=%d read=%d gaps=%d end=%08x\n",
                      depthName(depth), k, rep, s, !!(s & M_BUSY), s & 0xffff, (s >> 16) & 7, !!(s & M_EMPTY), cnt,
                      gaps, e);
}

static int s_fullAt;

/* In: decode command for 0x400 words, block words written with output never read. */
static void mdecInArm(int depth, int slow, int rep) {
    MDEC1 = 0x80000000;
    MDEC1 = 0;
    MDEC0 = 0x20000000 | (depth << 27) | 0x400;
    uint32_t s0 = MDEC1;
    int fullAt = -1, n;
    for (n = 0; n < 80; n++) {
        uint32_t t0 = now();
        MDEC0 = BLOCKWORD;
        uint32_t s = MDEC1;
        s_dt[n] = since(t0);
        if (slow) {
            delay(3000);
            s = MDEC1;
        }
        s_st[n] = s;
        if (s & M_FULL) {
            fullAt = n + 1;
            n++;
            break;
        }
    }
    delay(50000);
    uint32_t settled = MDEC1;
    ramsyscall_printf("OBS mdecin %s slow=%d rep=%d s0=%08x fullAt=%d settled=%08x rem@full=%04x consumed@full=%d\n",
                      depthName(depth), slow, rep, s0, fullAt, settled, s_st[n - 1] & 0xffff,
                      0x400 - 1 - (int)(s_st[n - 1] & 0xffff));
    ramsyscall_printf("OBS mdecin %s slow=%d rep=%d rem:", depthName(depth), slow, rep);
    for (int i = 0; i < n; i++) ramsyscall_printf(" %x", s_st[i] & 0xffff);
    ramsyscall_printf("\nOBS mdecin %s slow=%d rep=%d hi:", depthName(depth), slow, rep);
    for (int i = 0; i < n; i++) ramsyscall_printf(" %x", s_st[i] >> 16);
    ramsyscall_printf("\nOBS mdecin %s slow=%d rep=%d dt8:", depthName(depth), slow, rep);
    for (int i = 0; i < n; i++) ramsyscall_printf(" %u", s_dt[i]);
    ramsyscall_printf("\n");
    if (depth == 1 && slow && fullAt > 0) s_fullAt = fullAt;
    MDEC1 = 0x80000000;
    MDEC1 = 0;
}

/* Overfill: command for F+1 words, F written until full, then one more. If
   the extra word is kept the decode completes with (F+1)*16 words out; if it
   is dropped the decoder is left waiting with F*16 out. */
static void mdecOverfill(int f, int rep) {
    MDEC1 = 0x80000000;
    MDEC1 = 0;
    MDEC0 = 0x20000000 | (1 << 27) | (f + 1);
    int fullAt = -1;
    for (int n = 0; n < f; n++) {
        MDEC0 = BLOCKWORD;
        delay(3000);
        if ((MDEC1 & M_FULL) && fullAt < 0) fullAt = n + 1;
    }
    uint32_t sFull = MDEC1;
    uint32_t t0 = now();
    MDEC0 = BLOCKWORD;
    uint32_t sx = MDEC1;
    uint32_t dt = since(t0);
    int cnt = 0;
    for (int n = 0; n < 8192; n++) {
        int i = 0;
        while ((MDEC1 & M_EMPTY) && i < 20000) i++;
        if (i >= 20000) break;
        (void)MDEC0;
        cnt++;
    }
    uint32_t e = MDEC1;
    ramsyscall_printf("OBS mdecover rep=%d F=%d fullAt=%d sFull=%08x extraDt8=%u sAfter=%08x read=%d expectKept=%d expectLost=%d end=%08x busy=%d\n",
                      rep, f, fullAt, sFull, dt, sx, cnt, (f + 1) * 16, f * 16, e, !!(e & M_BUSY));
    MDEC1 = 0x80000000;
    MDEC1 = 0;
}

int main(void) {
    irqOff();
    T2_MODE = 0x200; /* sysclk/8 */
    ramsyscall_printf("FIFODEPTH-START\n");
    fullReset();
    GPU_STATUS = 0x10000007;
    ramsyscall_printf("OBS gpuver idx7=%08x idle=%08x\n", GPU_DATA, GPU_STATUS);

    for (int r = 0; r < REPS; r++) copyProof(r);
    for (int r = 0; r < REPS; r++) {
        gp0Arm("idle", 0, 40, r);
        gp0Arm("busy", 1, 40, r);
        gp0Arm("busy8", 1, 8, r);
    }
    for (int r = 0; r < REPS; r++) {
        grArm("nofill", GR_NOFILL, 0, r);
        grArm("fillbefore", GR_FILLBEFORE, 0, r);
        grArm("fillduring", GR_FILLDURING, 0, r);
        grArm("fillduring", GR_FILLDURING, 4, r);
        grArm("fillduring", GR_FILLDURING, 10, r);
        grTiming(r, 0);
        grTiming(r, 1);
    }

    mdecInit();
    for (int r = 0; r < REPS; r++) {
        for (int k = 1; k <= 8; k++) mdecOutArm(0, k, r);
        for (int k = 1; k <= 5; k++) mdecOutArm(1, k, r);
        mdecOutArm(2, 6, r); /* one colour macroblock, 192 words */
    }
    s_fullAt = -1;
    for (int r = 0; r < REPS; r++) {
        mdecInArm(0, 0, r);
        mdecInArm(0, 1, r);
        mdecInArm(1, 0, r);
        mdecInArm(1, 1, r);
        mdecInArm(2, 1, r);
    }
    if (s_fullAt > 0)
        for (int r = 0; r < REPS; r++) mdecOverfill(s_fullAt, r);
    else
        ramsyscall_printf("OBS mdecover skipped: no fullAt\n");

    GPU_STATUS = 0x00000000;
    ramsyscall_printf("OBS done\n");
    return 0;
}
