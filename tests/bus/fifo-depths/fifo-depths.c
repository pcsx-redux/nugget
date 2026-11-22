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

/* v2 GP0 arm. v1 showed GP0 writes into a full FIFO never stall the CPU and
   the A0h upload desynchronises, so the queued traffic is now self-
   resynchronising: e GP0(E6h) words then P GP0(68h) 1x1 dots, dot i at
   (DOT_X+i, DOT_Y) in a colour that encodes i. A vertex word has top byte 0
   (y < 256), so if the command word before it is lost it parses as a NOP.
   After the copy, one GP0(00h) absorbs a dangling vertex, GP1(01h) clears
   any half command, and the row is read back: 'o' = dot i drawn with its
   own colour, '.' = not drawn, '?' = something else. */
#define DOT_X 640
#define DOT_Y 100
static inline uint32_t dotCol(int i) {
    return (0xf8u << 16) | ((uint32_t)((16 + (i >> 5)) << 3) << 8) | ((uint32_t)(i & 31) << 3);
}
static inline uint32_t dotPx(int i) { return (uint32_t)(i & 31) | ((uint32_t)(16 + (i >> 5)) << 5) | (0x1fu << 10); }

static void drawEnv(void) {
    waitIdle();
    GPU_DATA = 0xe1000400;
    GPU_DATA = 0xe3000000;
    GPU_DATA = 0xe4000000 | (511 << 10) | 1023;
    GPU_DATA = 0xe5000000;
    waitIdle();
}

static void gp0Dots(int busy, int e, int p, int rep) {
    fullReset();
    fill(0, 0, 512, 256, 0x0000f8);
    fill(0, 256, 512, 256, 0xf80000);
    fill(DOT_X, DOT_Y, 128, 1, 0x000000);
    drawEnv();
    uint32_t tc = now();
    if (busy) startCopy();
    for (int i = 0; i < e; i++) GPU_DATA = 0xe6000000;
    for (int i = 0; i < p; i++) {
        GPU_DATA = 0x68000000 | dotCol(i);
        GPU_DATA = (DOT_Y << 16) | (DOT_X + i);
    }
    uint32_t tBurst = since(tc);
    uint32_t stBurst = GPU_STATUS;
    uint32_t polls = 0;
    while (!(GPU_STATUS & ST_EMPTY) && polls < 2000000) polls++;
    delay(20000); /* the copy and anything queued finish */
    uint32_t stPre = GPU_STATUS;
    GPU_DATA = 0x00000000;
    waitIdle();
    GPU_STATUS = 0x01000000;
    int idle = waitIdle();
    int to = readRect(DOT_X, DOT_Y, 128, 1, s_rb, 64);
    int present = 0, other = 0, beyond = 0, lastPresent = -1;
    char map[65];
    for (int i = 0; i < 64; i++) {
        uint32_t px = (i & 1) ? (s_rb[i >> 1] >> 16) : (s_rb[i >> 1] & 0xffff);
        if (i < p && px == dotPx(i)) {
            map[i] = 'o';
            present++;
            lastPresent = i;
        } else if (px == 0) {
            map[i] = '.';
        } else {
            map[i] = '?';
            if (i < p)
                other++;
            else
                beyond++;
        }
    }
    map[64] = 0;
    ramsyscall_printf(
        "OBS dots %s rep=%d e=%d P=%d words=%d present=%d lastPresent=%d other=%d beyond=%d tBurst8=%u drainPolls=%u stBurst=%08x stPre=%08x idle=%d rbTO=%d map=%s\n",
        busy ? "busy" : "idle", rep, e, p, e + 2 * p, present, lastPresent, other, beyond, tBurst, polls, stBurst, stPre,
        idle, to, map);
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

/* v2 GPUREAD arm. v1: a GP0(02h) fill written during a C0h transfer only runs
   after the transfer, so it cannot mark the read FIFO. Here, after j words,
   the transfer is cut by GP1(01h) (mode 1) or GP1(00h) (mode 2); then 40 raw
   reads. Words that still continue the pattern from j were already fetched.
   Mode 0 = no cut (control: all 40 continue). */
static void printPat(uint32_t v, int notReady) {
    if ((v & 0xf000f000u) == 0x50005000u && (v >> 16) == (v & 0xffff) + 1)
        ramsyscall_printf(" p%d%s", (int)((v & 0xfff) >> 1), notReady ? "!" : "");
    else
        ramsyscall_printf(" %08x%s", v, notReady ? "!" : "");
}

static void grAbort(int mode, int j, int rep) {
    fullReset();
    uploadPattern();
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (GR_Y << 16) | GR_X;
    GPU_DATA = (1 << 16) | GR_W;
    delay(3000);
    for (int k = 0; k < j; k++) {
        int i = 0;
        while (!(GPU_STATUS & ST_READ_RDY) && i < 100000) i++;
        s_rb[k] = GPU_DATA;
    }
    uint32_t st0 = GPU_STATUS;
    if (mode == 1) GPU_STATUS = 0x01000000;
    if (mode == 2) GPU_STATUS = 0x00000000;
    delay(3000);
    uint32_t st1 = GPU_STATUS;
    for (int k = 0; k < 40; k++) {
        s_st[k] = GPU_STATUS;
        s_rb[j + k] = GPU_DATA;
    }
    int cont = 0;
    while (cont < 40 && s_rb[j + cont] == patWord(j + cont)) cont++;
    int okJ = 0;
    for (int k = 0; k < j; k++) okJ += s_rb[k] == patWord(k);
    ramsyscall_printf("OBS grabort mode=%d j=%d rep=%d okJ=%d cont=%d st0=%08x st1=%08x raw:", mode, j, rep, okJ, cont,
                      st0, st1);
    for (int k = 0; k < 40; k++) printPat(s_rb[j + k], !(s_st[k] & ST_READ_RDY));
    ramsyscall_printf("\n");
    fullReset();
    waitIdle();
}

/* What GPUREAD returns before the first word is ready and past the end of a
   16-word transfer. '!' = bit 27 was clear just before that read. */
static void grEnd(int rep) {
    fullReset();
    uploadPattern();
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (GR_Y << 16) | GR_X;
    GPU_DATA = (1 << 16) | 32;
    uint32_t s0 = GPU_STATUS;
    uint32_t r0 = GPU_DATA;
    delay(3000);
    ramsyscall_printf("OBS grend rep=%d immediate:", rep);
    printPat(r0, !(s0 & ST_READ_RDY));
    ramsyscall_printf(" then:");
    for (int k = 0; k < 24; k++) {
        uint32_t s = GPU_STATUS;
        uint32_t v = GPU_DATA;
        printPat(v, !(s & ST_READ_RDY));
    }
    ramsyscall_printf(" end=%08x\n", GPU_STATUS);
    fullReset();
    waitIdle();
}

/* v3 GPUREAD arms. v2: GP1(01h)/(00h) leave exactly one readable word, so a
   cut cannot see past the output latch. Two rate-based instruments instead:
   reads that outrun the GPU's refill come back as a repeat of the previous
   word (grEnd: a not-ready read does not advance).
   grBurst: 32 back-to-back CPU loads, stored to the scratchpad.
   grDma:   DMA2 VRAM->RAM sync 1, GP1(04h)=3, BS words per block. A block
            larger than what the GPU has buffered when DREQ fires should show
            a repeat inside the block. */
#define SPADW ((volatile uint32_t *)0x1f800000)
#define R4(k) SPADW[k] = GPU_DATA; SPADW[k + 1] = GPU_DATA; SPADW[k + 2] = GPU_DATA; SPADW[k + 3] = GPU_DATA;
static void grBurst(int pre, int rep) {
    fullReset();
    uploadPattern();
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (GR_Y << 16) | GR_X;
    GPU_DATA = (1 << 16) | GR_W;
    delay(pre);
    uint32_t t0 = now();
    R4(0) R4(4) R4(8) R4(12) R4(16) R4(20) R4(24) R4(28)
    uint32_t dt = since(t0);
    int rpt = 0, firstRpt = -1, ok = 0;
    for (int k = 0; k < 32; k++) {
        if (k && SPADW[k] == SPADW[k - 1]) {
            rpt++;
            if (firstRpt < 0) firstRpt = k;
        }
    }
    int base = -1;
    for (int k = 0; k < 32; k++) {
        if (SPADW[k] == patWord(k)) ok++;
    }
    ramsyscall_printf("OBS grburst pre=%d rep=%d dt8=%u ok=%d repeats=%d firstRepeat=%d raw:", pre, rep, dt, ok, rpt,
                      firstRpt);
    for (int k = 0; k < 32; k++) printPat(SPADW[k], 0);
    ramsyscall_printf("\n");
    (void)base;
    fullReset();
    waitIdle();
}

#define D2_MADR (*(volatile uint32_t *)0xbf8010a0)
#define D2_BCR (*(volatile uint32_t *)0xbf8010a4)
#define D2_CHCR (*(volatile uint32_t *)0xbf8010a8)
static uint32_t s_dbuf[160];
static int patIndex(uint32_t v) {
    if ((v & 0xf000f000u) == 0x50005000u && (v >> 16) == (v & 0xffff) + 1 && !(v & 1)) return (int)((v & 0xfff) >> 1);
    return -1;
}

/* v4: pre = delay between the C0h header and the DMA start (lets the GPU
   fill whatever it buffers); raw printed as runs of pattern indices. */
static void grDma(int bs, int ba, int pre, int rep) {
    int words = bs * ba;
    fullReset();
    uploadPattern();
    waitIdle();
    for (int k = 0; k < 160; k++) s_dbuf[k] = 0xdeadbeef;
    GPU_STATUS = 0x04000003; /* DREQ = read data ready */
    GPU_DATA = 0xc0000000;
    GPU_DATA = (GR_Y << 16) | GR_X;
    GPU_DATA = (1 << 16) | (words * 2);
    delay(pre);
    uint32_t st0 = GPU_STATUS;
    uint32_t dpcr0 = DPCR;
    DPCR = dpcr0 | 0x00000800u;
    D2_MADR = (uint32_t)s_dbuf & 0x1fffffff;
    D2_BCR = ((uint32_t)ba << 16) | bs;
    uint32_t t0 = now();
    D2_CHCR = 0x01000200; /* device->RAM, sync 1, start */
    uint32_t polls = 0;
    while ((D2_CHCR & 0x01000000) && polls < 1000000) polls++;
    uint32_t dt = since(t0);
    int aborted = 0;
    if (D2_CHCR & 0x01000000) {
        D2_CHCR = 0;
        aborted = 1;
    }
    uint32_t bcr = D2_BCR, madr = D2_MADR, st = GPU_STATUS;
    DPCR = dpcr0;
    volatile uint32_t *b = (volatile uint32_t *)(((uint32_t)s_dbuf & 0x1fffffff) | 0xa0000000);
    int ok = 0, rpt = 0, firstBad = -1;
    for (int k = 0; k < words; k++) {
        if (b[k] == patWord(k))
            ok++;
        else if (firstBad < 0)
            firstBad = k;
        if (k && b[k] == b[k - 1]) rpt++;
    }
    ramsyscall_printf(
        "OBS grdma bs=%d ba=%d pre=%d rep=%d words=%d ok=%d repeats=%d firstBad=%d aborted=%d polls=%u dt8=%u bcr=%08x madr+%d st0=%08x st=%08x runs:",
        bs, ba, pre, rep, words, ok, rpt, firstBad, aborted, polls, dt, bcr, (int)(madr - ((uint32_t)s_dbuf & 0x1fffffff)),
        st0, st);
    /* runs: "[pos]a-b" = buffer positions from pos hold pattern words a..b */
    int k = 0;
    while (k < words) {
        int a = patIndex(b[k]);
        if (a < 0) {
            ramsyscall_printf(" [%d]%08x", k, b[k]);
            k++;
            continue;
        }
        int m = k + 1;
        while (m < words && patIndex(b[m]) == a + (m - k)) m++;
        ramsyscall_printf(" [%d]%d-%d", k, a, a + (m - k) - 1);
        k = m;
    }
    ramsyscall_printf("\n");
    fullReset();
    waitIdle();
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
/* v2: v1's N=k commands with k < 32 left the MDEC eating later command words
   as data. The command is now N=n words: k block words, then n-k padding
   words FE00FE00h (skipped before a DC). firstEmpty = read index at which
   bit 31 was first seen set. */
#define PADWORD 0xfe00fe00u
static uint32_t s_mo[8];
static void mdecOutArm(int depth, int k, int n, int rep) {
    MDEC1 = 0x80000000;
    MDEC1 = 0;
    delay(1000);
    MDEC0 = 0x20000000 | (depth << 27) | n;
    uint32_t sc = MDEC1;
    for (int i = 0; i < k; i++) MDEC0 = BLOCKWORD;
    for (int i = k; i < n; i++) MDEC0 = PADWORD;
    delay(50000);
    uint32_t s = MDEC1;
    int cnt = 0, gaps = 0, firstEmpty = -1;
    uint32_t last = 0;
    for (int m = 0; m < 4096; m++) {
        uint32_t t = MDEC1;
        if (t & M_EMPTY) {
            if (firstEmpty < 0) firstEmpty = cnt;
            int i = 0;
            while ((MDEC1 & M_EMPTY) && i < 20000) i++;
            if (i >= 20000) break;
            gaps++;
        }
        last = MDEC0;
        if (cnt < 8) s_mo[cnt] = last;
        cnt++;
    }
    uint32_t e = MDEC1;
    ramsyscall_printf(
        "OBS mdecout %s k=%d n=%d rep=%d afterCmd=%08x undrained=%08x busy=%d rem=%04x blk=%d empty=%d read=%d firstEmpty=%d gaps=%d end=%08x out0..3=%08x %08x %08x %08x last=%08x\n",
        depthName(depth), k, n, rep, sc, s, !!(s & M_BUSY), s & 0xffff, (s >> 16) & 7, !!(s & M_EMPTY), cnt, firstEmpty,
        gaps, e, s_mo[0], s_mo[1], s_mo[2], s_mo[3], last);
    MDEC1 = 0x80000000;
    MDEC1 = 0;
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
static void mdecOverfill(int f, int drainFirst, int rep) {
    MDEC1 = 0x80000000;
    MDEC1 = 0;
    delay(1000);
    MDEC0 = 0x20000000 | (1 << 27) | (f + 1);
    int fullAt = -1;
    for (int n = 0; n < f; n++) {
        MDEC0 = BLOCKWORD;
        delay(3000);
        if ((MDEC1 & M_FULL) && fullAt < 0) fullAt = n + 1;
    }
    uint32_t sFull = MDEC1;
    int pre = 0;
    if (drainFirst) {
        /* control: let the decoder empty its input before the extra word */
        for (int n = 0; n < 8192; n++) {
            int i = 0;
            while ((MDEC1 & M_EMPTY) && i < 20000) i++;
            if (i >= 20000) break;
            (void)MDEC0;
            pre++;
        }
    }
    uint32_t sMid = MDEC1;
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
    ramsyscall_printf(
        "OBS mdecover drainFirst=%d rep=%d F=%d fullAt=%d sFull=%08x readBefore=%d sMid=%08x extraDt8=%u sAfter=%08x readAfter=%d total=%d expectKept=%d expectLost=%d end=%08x busy=%d\n",
        drainFirst, rep, f, fullAt, sFull, pre, sMid, dt, sx, cnt, pre + cnt, (f + 1) * 16, f * 16, e, !!(e & M_BUSY));
    MDEC1 = 0x80000000;
    MDEC1 = 0;
}

int main(void) {
    irqOff();
    T2_MODE = 0x200; /* sysclk/8 */
    ramsyscall_printf("FIFODEPTH-START v4\n");
    fullReset();
    GPU_STATUS = 0x10000007;
    ramsyscall_printf("OBS gpuver idx7=%08x idle=%08x\n", GPU_DATA, GPU_STATUS);

    for (int r = 0; r < REPS; r++) copyProof(r);
    static const int ps[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 20};
    for (int r = 0; r < REPS; r++) {
        gp0Dots(0, 0, 20, r);
        gp0Dots(0, 1, 20, r);
        for (int e = 0; e < 2; e++)
            for (unsigned i = 0; i < sizeof(ps) / sizeof(ps[0]); i++) gp0Dots(1, e, ps[i], r);
    }
    grEnd(0);
    grAbort(0, 0, 0);
    grAbort(1, 0, 0);
    grAbort(1, 20, 0);
    grAbort(2, 20, 0);
    for (int r = 0; r < REPS; r++) {
        grBurst(0, r);
        grBurst(3000, r);
    }

    mdecInit();
    for (int r = 0; r < REPS; r++) {
        mdecOutArm(0, 1, 1, r); /* v1 shape, now with raw output */
        for (int k = 1; k <= 8; k++) mdecOutArm(0, k, 32, r);
        mdecOutArm(0, 12, 32, r);
        mdecOutArm(0, 16, 32, r);
        mdecOutArm(0, 32, 32, r);
        for (int k = 1; k <= 6; k++) mdecOutArm(1, k, 32, r);
        mdecOutArm(1, 8, 32, r);
        mdecOutArm(1, 32, 32, r);
        mdecOutArm(2, 6, 32, r);
        mdecOutArm(2, 12, 32, r);
        mdecOutArm(2, 30, 32, r);
        mdecOutArm(3, 6, 32, r);
        mdecOutArm(3, 12, 32, r);
        mdecOutArm(3, 30, 32, r);
    }
    s_fullAt = -1;
    for (int r = 0; r < REPS; r++) {
        mdecInArm(0, 0, r);
        mdecInArm(1, 1, r);
        mdecInArm(2, 1, r);
    }
    if (s_fullAt > 0)
        for (int r = 0; r < REPS; r++) {
            mdecOverfill(s_fullAt, 0, r);
            mdecOverfill(s_fullAt, 1, r);
        }
    else
        ramsyscall_printf("OBS mdecover skipped: no fullAt\n");

    /* DMA last: a stuck channel is aborted after a bounded poll, but keep
       everything else ahead of it. */
    static const int bss[][2] = {{8, 16}, {16, 8}, {17, 7}, {18, 7}, {20, 6}, {22, 5}, {24, 5}, {28, 4}, {32, 4}, {64, 2}, {128, 1}};
    for (int r = 0; r < REPS; r++)
        for (int pre = 0; pre < 2; pre++)
            for (unsigned i = 0; i < sizeof(bss) / sizeof(bss[0]); i++) grDma(bss[i][0], bss[i][1], pre ? 3000 : 0, r);

    GPU_STATUS = 0x00000000;
    ramsyscall_printf("OBS done\n");
    return 0;
}
