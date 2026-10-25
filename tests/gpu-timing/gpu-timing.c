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
 * GPU draw and transfer time per primitive class, against size.
 *
 * Each sample writes one command to GP0 from the CPU (or sends it through DMA
 * channel 2) and counts root counter 2 cycles (system clock) until GPUSTAT
 * bits 26 and 28 both read 1. The counter is unwrapped on every poll, so a
 * sample can run past 65536 cycles. Every sample is paired with a baseline:
 * the same number of GP0(00h) words through the same loop. Five pairs per
 * point; the line prints min, median and max of the raw time and the median
 * baseline, in CPU cycles.
 *
 * The display is on, 320x240 15-bit NTSC, showing the area being drawn.
 * Arm D repeats some classes with the display disabled (GP1(03h)).
 *
 * Output lines, one per point:
 *   P arm quad gouraud tex semi dither x w h min med max base list
 *     tex: 0 untextured, 4/8/15 texture depth. semi: 0 opaque, 1..4 is
 *     GP0(E1h) blend mode 0..3. list: median per primitive of a DMA linked
 *     list of 16 copies (sizes up to 64 only, else 0).
 *   R arm size tex semi raw w h min med max base     (size 0 variable,
 *     1/8/16 the fixed-size commands)
 *   L arm kind dither semi segs len min med max base
 *   F w h min med max base                            GP0(02h)
 *   C w h min med max base                            GP0(80h)
 *   U mode w h min med max base                       GP0(A0h), mode 0 CPU, 1 DMA2
 *   D mode w h min med max base                       GP0(C0h), mode 0 CPU, 1 DMA2
 * Every class also prints "K <tag> drawn=<0|1> grow=<0|1>": drawn reads one
 * pixel back after a draw over a cleared background, grow says the largest
 * size took longer than the smallest by the class's margin.
 *
 * Lines come from buildLineCommand() in lines.c.
 */

#include <stdint.h>

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"
#include "lines.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
#define R32(a) (*(volatile uint32_t *)(a))
#define D2_MADR R32(0xbf8010a0)
#define D2_BCR R32(0xbf8010a4)
#define D2_CHCR R32(0xbf8010a8)
#define D_DPCR R32(0xbf8010f0)
#define D_DICR R32(0xbf8010f4)
#define T2_VALUE R32(0xbf801120)
#define T2_MODE R32(0xbf801124)
#define GPU_READ R32(0xbf801810)

#define REPS 5
#define LIST_COPIES 16
#define LIST_MAX_SIZE 64
#define CMD_MAX 64
#define BIG_WORDS (240 * 240 / 2)
#define SENTINEL_FILL 0x080808u
#define SENTINEL_PIXEL 0x0421u
#define UPLOAD_Y 256
#define COPY_DST_X 256
#define COPY_DST_Y 256

#define TP4_X 512
#define TP8_X 640
#define TP15_X 768
#define CLUT4_Y 480
#define CLUT8_Y 481

static uint32_t s_cmd[CMD_MAX];
static const uint32_t s_zero[CMD_MAX];
static uint32_t s_list[LIST_COPIES * (CMD_MAX + 1)];
static uint32_t s_big[BIG_WORDS];
static int s_interruptsWereEnabled;

static uint32_t s_acc, s_prev;

static unsigned s_classes, s_badDraw, s_badGrow;
static unsigned s_lineClasses, s_lineBadDraw, s_lineBadGrow, s_lineUnbuilt;
static uint32_t s_idx7, s_idx7Primed;

static inline void tick(void) {
    uint32_t t = T2_VALUE & 0xffff;
    s_acc += (t - s_prev) & 0xffff;
    s_prev = t;
}

static inline void startClock(void) {
    s_prev = T2_VALUE & 0xffff;
    s_acc = 0;
}

static inline int gpuIdle(void) { return (GPU_STATUS & 0x14000000) == 0x14000000; }

static void waitIdle(void) {
    while (!gpuIdle()) {
    }
}

static inline void waitIdleTimed(void) {
    while (!gpuIdle()) tick();
    tick();
}

static uint32_t timeWords(const uint32_t *w, unsigned n, int pace) {
    waitIdle();
    startClock();
    for (unsigned i = 0; i < n; i++) {
        if (pace && (i & 7) == 0) {
            while (!(GPU_STATUS & 0x02000000)) tick();
        }
        GPU_DATA = w[i];
        if ((i & 15) == 15) tick();
    }
    waitIdleTimed();
    return s_acc;
}

static uint32_t timeList(const uint32_t *w, unsigned n) {
    for (unsigned k = 0; k < LIST_COPIES; k++) {
        uint32_t *node = &s_list[k * (n + 1)];
        uint32_t next = k + 1 < LIST_COPIES ? ((uint32_t)&s_list[(k + 1) * (n + 1)] & 0xffffff) : 0xffffff;
        node[0] = (n << 24) | next;
        for (unsigned i = 0; i < n; i++) node[i + 1] = w[i];
    }
    waitIdle();
    GPU_STATUS = 0x04000002;
    startClock();
    D2_MADR = (uint32_t)&s_list[0] & 0xffffff;
    D2_BCR = 0;
    D2_CHCR = 0x01000401;
    while (D2_CHCR & 0x01000000) tick();
    GPU_STATUS = 0x04000001;
    waitIdleTimed();
    return s_acc;
}

static void sort5(uint32_t *v) {
    for (int i = 1; i < REPS; i++) {
        uint32_t x = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
}

struct Sample {
    uint32_t min, med, max, base;
};

static struct Sample measure(const uint32_t *w, unsigned n, int pace) {
    uint32_t t[REPS], b[REPS];
    for (int r = 0; r < REPS; r++) {
        t[r] = timeWords(w, n, pace);
        b[r] = timeWords(s_zero, n, pace);
    }
    sort5(t);
    sort5(b);
    struct Sample s = {t[0], t[REPS / 2], t[REPS - 1], b[REPS / 2]};
    return s;
}

static uint32_t measureList(const uint32_t *w, unsigned n) {
    uint32_t t[REPS];
    for (int r = 0; r < REPS; r++) t[r] = timeList(w, n);
    sort5(t);
    return t[REPS / 2] / LIST_COPIES;
}

static uint32_t net(const struct Sample *s) { return s->med > s->base ? s->med - s->base : 0; }

static void gp0Fill(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color) {
    waitIdle();
    GPU_DATA = 0x02000000 | color;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (h << 16) | w;
    waitIdle();
}

static uint32_t readWord(unsigned x, unsigned y) {
    waitIdle();
    GPU_DATA = 0xc0000000;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (1 << 16) | 2;
    while (!(GPU_STATUS & 0x08000000)) {
    }
    uint32_t v = GPU_READ;
    waitIdle();
    return v;
}

static uint16_t readPixel(unsigned x, unsigned y) { return readWord(x, y) & 0xffff; }

static void uploadRect(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t (*gen)(unsigned, unsigned)) {
    waitIdle();
    GPU_DATA = 0xa0000000;
    GPU_DATA = (y << 16) | x;
    GPU_DATA = (h << 16) | w;
    unsigned i = 0;
    for (unsigned yy = 0; yy < h; yy++) {
        for (unsigned xx = 0; xx < w; xx += 2, i++) {
            if ((i & 7) == 0) {
                while (!(GPU_STATUS & 0x02000000)) {
                }
            }
            GPU_DATA = (uint32_t)gen(xx, yy) | ((uint32_t)gen(xx + 1, yy) << 16);
        }
    }
    waitIdle();
}

static uint16_t genTexel15(unsigned x, unsigned y) {
    uint16_t v = (uint16_t)((x * 0x0843u + y * 0x1085u + 0x0421u) & 0x7fff);
    if (!v) v = 1;
    return v | 0x8000;
}

static uint16_t genIndex(unsigned x, unsigned y) { return (uint16_t)(x * 0x3197u + y * 0x5ac3u); }

static uint16_t genClut(unsigned x, unsigned y) {
    (void)y;
    uint16_t v = (uint16_t)((x * 0x0c63u + 0x0842u) & 0x7fff);
    if (!v) v = 1;
    return v | 0x8000;
}

static void setupGpu(void) {
    GPU_STATUS = 0x00000000;
    GPU_STATUS = 0x06000000 | 0x260 | (0xc60 << 12);
    GPU_STATUS = 0x07000000 | 0x010 | (0x100 << 10);
    GPU_STATUS = 0x05000000;
    GPU_STATUS = 0x08000001;
    GPU_DATA = 0xe1000400;
    GPU_DATA = 0xe2000000;
    GPU_DATA = 0xe3000000;
    GPU_DATA = 0xe4000000 | 1023 | (511 << 10);
    GPU_DATA = 0xe5000000;
    GPU_DATA = 0xe6000000;
    GPU_STATUS = 0x03000000;
    GPU_STATUS = 0x04000001;
    waitIdle();
    uploadRect(TP4_X, 0, 64, 256, genIndex);
    uploadRect(TP8_X, 0, 128, 256, genIndex);
    uploadRect(TP15_X, 0, 256, 256, genTexel15);
    uploadRect(512, CLUT4_Y, 16, 1, genClut);
    uploadRect(512, CLUT8_Y, 256, 1, genClut);
}

static uint32_t tpageX(unsigned tex) { return tex == 4 ? TP4_X / 64 : tex == 8 ? TP8_X / 64 : TP15_X / 64; }
static uint32_t tdepth(unsigned tex) { return tex == 4 ? 0 : tex == 8 ? 1 : 2; }
static uint32_t clutWord(unsigned tex) {
    return tex == 4 ? (512 / 16) | (CLUT4_Y << 6) : tex == 8 ? (512 / 16) | (CLUT8_Y << 6) : 0;
}
static uint32_t abr(unsigned semi) { return semi ? semi - 1 : 0; }

static void setE1(unsigned tex, unsigned semi, unsigned dither) {
    uint32_t v = (abr(semi) << 5) | (1u << 10) | (dither << 9);
    if (tex) v |= tpageX(tex) | (tdepth(tex) << 7);
    waitIdle();
    GPU_DATA = 0xe1000000 | v;
    waitIdle();
}

static uint32_t vtx(unsigned x, unsigned y) { return (y << 16) | x; }
static uint32_t uvw(unsigned u, unsigned v) {
    if (u > 255) u = 255;
    if (v > 255) v = 255;
    return (v << 8) | u;
}

static const uint32_t s_colors[4] = {0x2050a0, 0xa05020, 0x50a020, 0x606060};

static unsigned buildPoly(uint32_t *o, unsigned quad, unsigned g, unsigned tex, unsigned semi, unsigned x0,
                          unsigned w, unsigned h) {
    unsigned op = 0x20 | (g ? 0x10 : 0) | (quad ? 0x08 : 0) | (tex ? 0x04 : 0) | (semi ? 0x02 : 0);
    unsigned nv = quad ? 4 : 3;
    unsigned vx[4] = {x0, x0 + w, x0, x0 + w};
    unsigned vy[4] = {0, 0, h, h};
    unsigned n = 0;
    uint32_t c0 = (tex && !g) ? 0x808080 : s_colors[0];
    uint32_t tpage = tex ? (tpageX(tex) | (abr(semi) << 5) | (tdepth(tex) << 7)) : 0;
    o[n++] = (op << 24) | c0;
    for (unsigned k = 0; k < nv; k++) {
        if (k && g) o[n++] = s_colors[k];
        o[n++] = vtx(vx[k], vy[k]);
        if (tex) {
            uint32_t hi = k == 0 ? clutWord(tex) : k == 1 ? tpage : 0;
            o[n++] = uvw(vx[k] - x0, vy[k]) | (hi << 16);
        }
    }
    return n;
}

static unsigned buildRect(uint32_t *o, unsigned size, unsigned tex, unsigned semi, unsigned raw, unsigned w,
                          unsigned h) {
    unsigned code = size == 0 ? 0 : size == 1 ? 1 : size == 8 ? 2 : 3;
    unsigned op = 0x60 | (code << 3) | (tex ? 0x04 : 0) | (semi ? 0x02 : 0) | (raw ? 0x01 : 0);
    unsigned n = 0;
    o[n++] = (op << 24) | (tex ? 0x808080 : 0x50a020);
    o[n++] = vtx(0, 0);
    if (tex) o[n++] = clutWord(tex) << 16;
    if (!size) o[n++] = (h << 16) | w;
    return n;
}

static void classDone(char kind, unsigned tag, int drawn, int grow, int isLine) {
    ramsyscall_printf("K %c %x drawn=%d grow=%d\n", kind, tag, drawn, grow);
    if (isLine) {
        s_lineClasses++;
        if (!drawn) s_lineBadDraw++;
        if (!grow) s_lineBadGrow++;
    } else {
        s_classes++;
        if (!drawn) s_badDraw++;
        if (!grow) s_badGrow++;
    }
}

static const unsigned s_polySizes[] = {1, 2, 4, 8, 16, 32, 64, 96, 128, 160, 192, 240};
#define N_POLY_SIZES (sizeof(s_polySizes) / sizeof(s_polySizes[0]))

static int polyDrawn(unsigned quad, unsigned g, unsigned tex, unsigned semi) {
    gp0Fill(0, 0, 256, 256, SENTINEL_FILL);
    unsigned n = buildPoly(s_cmd, quad, g, tex, semi, 0, 240, 240);
    timeWords(s_cmd, n, 0);
    waitIdle();
    return readPixel(60, 60) != SENTINEL_PIXEL;
}

static void polyPoint(char arm, unsigned quad, unsigned g, unsigned tex, unsigned semi, unsigned d, unsigned x0,
                      unsigned w, unsigned h, struct Sample *out) {
    unsigned n = buildPoly(s_cmd, quad, g, tex, semi, x0, w, h);
    struct Sample s = measure(s_cmd, n, 0);
    uint32_t list = (w <= LIST_MAX_SIZE && h <= LIST_MAX_SIZE && arm == 'M') ? measureList(s_cmd, n) : 0;
    ramsyscall_printf("P %c %d %d %d %d %d %d %d %d %d %d %d %d %d\n", arm, quad, g, tex, semi, d, x0, w, h, s.min,
                      s.med, s.max, s.base, list);
    if (out) *out = s;
}

static void polyClass(char arm, unsigned quad, unsigned g, unsigned tex, unsigned semi, unsigned d) {
    setE1(tex, semi, d);
    struct Sample first = {0}, last = {0};
    for (unsigned i = 0; i < N_POLY_SIZES; i++) {
        unsigned L = s_polySizes[i];
        struct Sample s;
        polyPoint(arm, quad, g, tex, semi, d, 0, L, L, &s);
        if (i == 0) first = s;
        last = s;
    }
    int drawn = polyDrawn(quad, g, tex, semi);
    unsigned tag = (quad << 16) | (g << 12) | (tex << 4) | (semi << 1) | d;
    classDone(arm, tag, drawn, net(&last) > net(&first) + 1000, 0);
}

static void polyShapes(unsigned quad, unsigned g, unsigned tex, unsigned semi, unsigned d) {
    setE1(tex, semi, d);
    for (unsigned i = 0; i < N_POLY_SIZES; i++) {
        unsigned L = s_polySizes[i];
        polyPoint('W', quad, g, tex, semi, d, 0, L, 8, 0);
        polyPoint('H', quad, g, tex, semi, d, 0, 8, L, 0);
    }
    for (unsigned x0 = 0; x0 < 16; x0++) polyPoint('X', quad, g, tex, semi, d, x0, 64, 64, 0);
}

static const unsigned s_rectSizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 240};
#define N_RECT_SIZES (sizeof(s_rectSizes) / sizeof(s_rectSizes[0]))

static void rectPoint(char arm, unsigned size, unsigned tex, unsigned semi, unsigned raw, unsigned w, unsigned h,
                      struct Sample *out) {
    unsigned n = buildRect(s_cmd, size, tex, semi, raw, w, h);
    struct Sample s = measure(s_cmd, n, 0);
    ramsyscall_printf("R %c %d %d %d %d %d %d %d %d %d %d\n", arm, size, tex, semi, raw, w, h, s.min, s.med, s.max,
                      s.base);
    if (out) *out = s;
}

static void rectClass(char arm, unsigned tex, unsigned semi, unsigned raw) {
    setE1(tex, semi, 0);
    struct Sample first = {0}, last = {0};
    for (unsigned i = 0; i < N_RECT_SIZES; i++) {
        unsigned L = s_rectSizes[i];
        struct Sample s;
        rectPoint(arm, 0, tex, semi, raw, L, L, &s);
        if (i == 0) first = s;
        last = s;
    }
    rectPoint(arm, 1, tex, semi, raw, 1, 1, 0);
    rectPoint(arm, 8, tex, semi, raw, 8, 8, 0);
    rectPoint(arm, 16, tex, semi, raw, 16, 16, 0);
    gp0Fill(0, 0, 256, 256, SENTINEL_FILL);
    unsigned n = buildRect(s_cmd, 0, tex, semi, raw, 240, 240);
    timeWords(s_cmd, n, 0);
    int drawn = readPixel(60, 60) != SENTINEL_PIXEL;
    unsigned tag = 0x100000 | (tex << 4) | (semi << 1) | raw;
    classDone(arm, tag, drawn, net(&last) > net(&first) + 1000, 0);
}

static const unsigned s_lineLens[] = {1, 2, 4, 8, 16, 32, 64, 128, 255};
#define N_LINE_LENS (sizeof(s_lineLens) / sizeof(s_lineLens[0]))
static const unsigned s_lineSegs[] = {2, 4, 8, 16};

static void lineClass(unsigned kind, unsigned semi, unsigned d) {
    unsigned k = kind | (semi ? LINE_SEMI : 0);
    if (!buildLineCommand(s_cmd, k, 1, (kind & LINE_POLY) ? 2 : 1)) {
        s_lineUnbuilt++;
        ramsyscall_printf("L kind %d unbuilt\n", k);
        return;
    }
    setE1(0, semi, d);
    struct Sample first = {0}, last = {0};
    unsigned segs = (kind & LINE_POLY) ? 2 : 1;
    for (unsigned i = 0; i < N_LINE_LENS; i++) {
        unsigned n = buildLineCommand(s_cmd, k, s_lineLens[i], segs);
        struct Sample s = measure(s_cmd, n, 0);
        ramsyscall_printf("L M %d %d %d %d %d %d %d %d %d\n", k, d, semi, segs, s_lineLens[i], s.min, s.med, s.max,
                          s.base);
        if (i == 0) first = s;
        last = s;
    }
    if (kind & LINE_POLY) {
        for (unsigned i = 0; i < sizeof(s_lineSegs) / sizeof(s_lineSegs[0]); i++) {
            unsigned n = buildLineCommand(s_cmd, k, 64, s_lineSegs[i]);
            struct Sample s = measure(s_cmd, n, 0);
            ramsyscall_printf("L S %d %d %d %d %d %d %d %d %d\n", k, d, semi, s_lineSegs[i], 64, s.min, s.med, s.max,
                              s.base);
        }
    }
    gp0Fill(0, 0, 256, 256, SENTINEL_FILL);
    unsigned n = buildLineCommand(s_cmd, k, 255, segs);
    timeWords(s_cmd, n, 0);
    int drawn = readPixel(0, 0) != SENTINEL_PIXEL;
    classDone('L', 0x200000 | (k << 4) | (semi << 1) | d, drawn, net(&last) > net(&first) + 64, 1);
}

static const unsigned s_fillSizes[] = {16, 32, 48, 64, 128, 256};

static void fillClass(void) {
    struct Sample first = {0}, last = {0};
    for (unsigned i = 0; i < sizeof(s_fillSizes) / sizeof(s_fillSizes[0]); i++) {
        unsigned L = s_fillSizes[i];
        s_cmd[0] = 0x02000000 | 0x50a020;
        s_cmd[1] = 0;
        s_cmd[2] = (L << 16) | L;
        struct Sample s = measure(s_cmd, 3, 0);
        ramsyscall_printf("F %d %d %d %d %d %d\n", L, L, s.min, s.med, s.max, s.base);
        if (i == 0) first = s;
        last = s;
    }
    gp0Fill(0, 0, 256, 256, SENTINEL_FILL);
    gp0Fill(0, 0, 16, 16, 0x00f8f8);
    classDone('F', 0x300000, readPixel(0, 0) == 0x03ff, net(&last) > net(&first) + 1000, 0);
}

static void copyClass(void) {
    struct Sample first = {0}, last = {0};
    gp0Fill(0, 0, 256, 256, 0x00f8f8);
    for (unsigned i = 0; i < N_RECT_SIZES; i++) {
        unsigned L = s_rectSizes[i];
        s_cmd[0] = 0x80000000;
        s_cmd[1] = 0;
        s_cmd[2] = vtx(COPY_DST_X, COPY_DST_Y);
        s_cmd[3] = (L << 16) | L;
        struct Sample s = measure(s_cmd, 4, 0);
        ramsyscall_printf("C %d %d %d %d %d %d\n", L, L, s.min, s.med, s.max, s.base);
        if (i == 0) first = s;
        last = s;
    }
    gp0Fill(COPY_DST_X, COPY_DST_Y, 256, 240, SENTINEL_FILL);
    s_cmd[3] = (240 << 16) | 240;
    timeWords(s_cmd, 4, 0);
    classDone('C', 0x400000, readPixel(COPY_DST_X + 60, COPY_DST_Y + 60) == 0x03ff,
              net(&last) > net(&first) + 1000, 0);
}

struct Shape {
    uint16_t w, h;
};
static const struct Shape s_xferShapes[] = {{8, 8}, {16, 16}, {32, 32}, {64, 64}, {128, 128}, {240, 240}, {320, 16}, {16, 240}};
#define N_XFER (sizeof(s_xferShapes) / sizeof(s_xferShapes[0]))

static uint32_t timeUpload(unsigned w, unsigned h, int dma) {
    unsigned words = w * h / 2;
    waitIdle();
    startClock();
    GPU_DATA = 0xa0000000;
    GPU_DATA = vtx(0, UPLOAD_Y);
    GPU_DATA = (h << 16) | w;
    if (dma) {
        GPU_STATUS = 0x04000002;
        D2_MADR = (uint32_t)s_big & 0xffffff;
        D2_BCR = 16 | ((words / 16) << 16);
        D2_CHCR = 0x01000201;
        while (D2_CHCR & 0x01000000) tick();
        GPU_STATUS = 0x04000001;
    } else {
        for (unsigned i = 0; i < words; i++) {
            if ((i & 7) == 0) {
                while (!(GPU_STATUS & 0x02000000)) tick();
            }
            GPU_DATA = s_big[i];
            if ((i & 15) == 15) tick();
        }
    }
    waitIdleTimed();
    return s_acc;
}

static uint32_t timeDownload(unsigned w, unsigned h, int dma) {
    unsigned words = w * h / 2;
    waitIdle();
    startClock();
    GPU_DATA = 0xc0000000;
    GPU_DATA = 0;
    GPU_DATA = (h << 16) | w;
    while (!(GPU_STATUS & 0x08000000)) tick();
    if (dma) {
        GPU_STATUS = 0x04000003;
        D2_MADR = (uint32_t)s_big & 0xffffff;
        D2_BCR = 16 | ((words / 16) << 16);
        D2_CHCR = 0x01000200;
        while (D2_CHCR & 0x01000000) tick();
        GPU_STATUS = 0x04000001;
    } else {
        for (unsigned i = 0; i < words; i++) {
            while (!(GPU_STATUS & 0x08000000)) tick();
            s_big[i] = GPU_READ;
            if ((i & 15) == 15) tick();
        }
    }
    waitIdleTimed();
    return s_acc;
}

static void xferClass(char kind, int dma) {
    struct Sample first = {0}, last = {0};
    for (unsigned i = 0; i < N_XFER; i++) {
        unsigned w = s_xferShapes[i].w, h = s_xferShapes[i].h;
        uint32_t t[REPS], b[REPS];
        for (int r = 0; r < REPS; r++) {
            if (kind == 'U') {
                for (unsigned j = 0; j < w * h / 2; j++) s_big[j] = 0x7c1f03e0u ^ (j * 0x00210021u);
                t[r] = timeUpload(w, h, dma);
            } else {
                t[r] = timeDownload(w, h, dma);
            }
            b[r] = timeWords(s_zero, 3, 0);
        }
        sort5(t);
        sort5(b);
        struct Sample s = {t[0], t[REPS / 2], t[REPS - 1], b[REPS / 2]};
        ramsyscall_printf("%c %d %d %d %d %d %d %d\n", kind, dma, w, h, s.min, s.med, s.max, s.base);
        if (i == 0) first = s;
        if (w == 240) last = s;
    }
    int ok;
    if (kind == 'U') {
        ok = readPixel(0, UPLOAD_Y) == (s_big[0] & 0xffff);
    } else {
        gp0Fill(0, 0, 16, 16, 0x00f8f8);
        s_big[0] = 0;
        timeDownload(16, 16, dma);
        ok = (s_big[0] & 0xffff) == 0x03ff;
    }
    classDone(kind, 0x500000 | dma, ok, net(&last) > net(&first) + 1000, 0);
}

static void probeRevision(void) {
    gp0Fill(0, 0, 16, 16, 0x00f8f8);
    s_idx7Primed = readWord(0, 0);
    GPU_STATUS = 0x10000007;
    for (volatile int i = 0; i < 64; i++) {
    }
    s_idx7 = GPU_READ;
    ramsyscall_printf("REV idx7=%08x primed=%08x\n", s_idx7, s_idx7Primed);
}

static void runAll(void) {
    D_DPCR |= 0x00000800;
    D_DICR = 0;
    T2_MODE = 0;
    setupGpu();
    probeRevision();
    ramsyscall_printf("# P arm quad gouraud tex semi dither x w h min med max base list\n");
    for (unsigned quad = 0; quad < 2; quad++)
        for (unsigned g = 0; g < 2; g++)
            for (unsigned ti = 0; ti < 4; ti++)
                for (unsigned semi = 0; semi < 5; semi++)
                    for (unsigned d = 0; d < 2; d++) polyClass('M', quad, g, ti == 0 ? 0 : ti == 1 ? 4 : ti == 2 ? 8 : 15, semi, d);
    polyShapes(1, 0, 0, 0, 0);
    polyShapes(1, 1, 15, 1, 1);
    polyShapes(1, 0, 4, 0, 0);
    polyShapes(1, 0, 8, 0, 0);
    ramsyscall_printf("# R arm size tex semi raw w h min med max base\n");
    for (unsigned ti = 0; ti < 4; ti++) {
        unsigned tex = ti == 0 ? 0 : ti == 1 ? 4 : ti == 2 ? 8 : 15;
        for (unsigned semi = 0; semi < 5; semi++)
            for (unsigned raw = 0; raw < (tex ? 2u : 1u); raw++) rectClass('M', tex, semi, raw);
    }
    ramsyscall_printf("# L arm kind dither semi segs len min med max base\n");
    for (unsigned kind = 0; kind < 4; kind++)
        for (unsigned semi = 0; semi < 5; semi++)
            for (unsigned d = 0; d < 2; d++) lineClass(kind, semi, d);
    fillClass();
    copyClass();
    xferClass('U', 0);
    xferClass('U', 1);
    xferClass('D', 0);
    xferClass('D', 1);
    GPU_STATUS = 0x03000001;
    polyClass('D', 1, 0, 0, 0, 0);
    polyClass('D', 1, 1, 15, 1, 1);
    polyClass('D', 1, 0, 4, 0, 0);
    rectClass('D', 0, 0, 0);
    rectClass('D', 15, 0, 1);
    GPU_STATUS = 0x03000000;
    ramsyscall_printf("SUMMARY classes=%d baddraw=%d badgrow=%d lines=%d linebaddraw=%d linebadgrow=%d lineunbuilt=%d\n",
                      s_classes, s_badDraw, s_badGrow, s_lineClasses, s_lineBadDraw, s_lineBadGrow, s_lineUnbuilt);
}

static int lineWordsOk(unsigned kind, unsigned segs) {
    uint32_t out[LINE_MAX_WORDS + 4];
    for (unsigned i = 0; i < LINE_MAX_WORDS + 4; i++) out[i] = 0xdeadbeef;
    unsigned n = buildLineCommand(out, kind, 32, segs);
    unsigned g = kind & LINE_GOURAUD, poly = kind & LINE_POLY;
    unsigned verts = poly ? segs + 1 : 2;
    unsigned want = 1 + verts + (g ? verts - 1 : 0) + (poly ? 1 : 0);
    if (n != want) return 0;
    unsigned op = 0x40 | (g ? 0x10 : 0) | (poly ? 0x08 : 0) | ((kind & LINE_SEMI) ? 0x02 : 0);
    if ((out[0] >> 24) != op) return 0;
    if ((out[0] & 0xffffff) == 0) return 0;
    if (out[1] != 0) return 0;
    if (poly && (out[n - 1] & 0xf000f000) != 0x50005000) return 0;
    if (out[n] != 0xdeadbeef) return 0;
    return 1;
}
)

CESTER_BEFORE_ALL(gputiming,
    s_interruptsWereEnabled = enterCriticalSection();
    IMASK = 0;
    runAll();
)

CESTER_AFTER_ALL(gputiming,
    D2_CHCR = 0x00000401;
    GPU_STATUS = 0x00000000;
    if (s_interruptsWereEnabled) leaveCriticalSection();
)

CESTER_MAYBE_TEST(everyClassDraws, gputiming,
    cester_assert_uint_ne(0, s_classes);
    cester_assert_uint_eq(0, s_badDraw);
)

CESTER_MAYBE_TEST(timeGrowsWithSize, gputiming,
    cester_assert_uint_eq(0, s_badGrow);
)

CESTER_TEST(lineCommandsAreWellFormed, gputiming,
    for (unsigned semi = 0; semi < 2; semi++) {
        unsigned s = semi ? LINE_SEMI : 0;
        cester_assert_int_eq(1, lineWordsOk(s, 1));
        cester_assert_int_eq(1, lineWordsOk(s | LINE_GOURAUD, 1));
        cester_assert_int_eq(1, lineWordsOk(s | LINE_POLY, 2));
        cester_assert_int_eq(1, lineWordsOk(s | LINE_POLY, LINE_MAX_SEGS));
        cester_assert_int_eq(1, lineWordsOk(s | LINE_POLY | LINE_GOURAUD, 2));
        cester_assert_int_eq(1, lineWordsOk(s | LINE_POLY | LINE_GOURAUD, LINE_MAX_SEGS));
    }
)

CESTER_MAYBE_TEST(lineTimeGrowsWithLength, gputiming,
    cester_assert_uint_eq(0, s_lineUnbuilt);
    cester_assert_uint_eq(40, s_lineClasses);
    cester_assert_uint_eq(0, s_lineBadDraw);
    cester_assert_uint_eq(0, s_lineBadGrow);
)
