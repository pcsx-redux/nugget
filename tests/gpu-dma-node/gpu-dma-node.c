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

// GPU DMA linked-list node length against a busy GPU, and GP0(E6h) ordering.
//
// GP0(E6h) is applied in order: a mask-setting change that follows a
// polygon in the same node does not reach that polygon (arms A-E).
//
// A single linked-list node is sent without waiting for the GPU: the
// channel finishes a node in one poll even while the GPU is still drawing,
// and only stalls between nodes. When a node carries more words than the
// GPU can buffer behind a slow polygon, the stream is corrupted:
//   S  one node: E6=1, 256x200 semi quad, k dots, E6=0. Up to 17 words
//      correct; 19 words draws half the quad; 21 words draws none of it;
//      23 words and up loses the dots.
//   G  39-word node after a semi quad: the dots are lost, even after a
//      4x4 quad. The same words split into nodes of 8 or fewer land.
//   P  a 23-word face node queued behind an opaque 256x200 quad: the
//      holes are corrupted (A118h instead of 8010h). Split 7+9+7, correct.
// Passes on an SCPH-1000, an SCPH-5501 and an SCPH-7001. Redux sends a
// has no GP0 FIFO and draws every arm correctly, so the corrupted arms are
// hardware-only. The 160x160 arms print only: their value check assumes
// 1:1 texel scale, and the single and split nodes read the same.

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

#include "common/hardware/dma.h"
#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

#include "raster-helpers.h"

CESTER_BODY(
static int s_ie;

#define BG 0x0010u          /* R5=16, bit15 clear */
#define BG_MASKED 0x8010u
#define POLL_MAX 20000000u

static uint32_t s_chain[80] __attribute__((aligned(8)));
static unsigned s_n;
static uint16_t s_row[512];

static uint32_t s_nodes[8][40] __attribute__((aligned(8)));
static unsigned s_brk[8], s_nb;
static void chainBegin(void) { s_n = 0; s_nb = 0; }
static void put(uint32_t w) { s_chain[1 + s_n++] = w; }
static void nodeBreak(void) { s_brk[s_nb++] = s_n; }
/* Build linked nodes from s_chain split at s_brk; returns head address. */
static uintptr_t buildNodes(char *desc) {
    if (s_nb == 0) {
        s_chain[0] = ((uint32_t)s_n << 24) | 0x00ffffffu;
        desc[0] = 0;
        return (uintptr_t)s_chain;
    }
    unsigned start = 0, k;
    char *d = desc;
    for (k = 0; k <= s_nb; k++) {
        unsigned end = k < s_nb ? s_brk[k] : s_n;
        unsigned n = end - start;
        for (unsigned i = 0; i < n; i++) s_nodes[k][1 + i] = s_chain[1 + start + i];
        uint32_t next = k < s_nb ? ((uintptr_t)s_nodes[k + 1] & 0x00ffffffu) : 0x00ffffffu;
        s_nodes[k][0] = ((uint32_t)n << 24) | next;
        if (n >= 10) *d++ = '0' + n / 10;
        *d++ = '0' + n % 10;
        *d++ = k < s_nb ? '+' : 0;
        start = end;
    }
    return (uintptr_t)s_nodes[0];
}

static unsigned waitIdle(void) {
    unsigned p = 0;
    while ((GPU_STATUS & 0x14000000u) != 0x14000000u && p < POLL_MAX) p++;
    return p;
}

static void chainRun(const char *tag) {
    unsigned pre = waitIdle();
    char desc[40];
    uintptr_t head = buildNodes(desc);
    GPU_STATUS = 0x04000002u;  /* GP1(04h)=2 CPU->GPU DMA */
    DMA_CTRL[DMA_GPU].MADR = head & 0x00ffffffu;
    DMA_CTRL[DMA_GPU].BCR = 0;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401u;  /* linked list, from RAM, start */
    unsigned dp = 0;
    while ((DMA_CTRL[DMA_GPU].CHCR & 0x01000000u) && dp < POLL_MAX) dp++;
    uint32_t statDone = GPU_STATUS;
    unsigned ip = waitIdle();
    uint32_t statIdle = GPU_STATUS;
    GPU_STATUS = 0x04000001u;  /* back to FIFO mode for readback */
    ramsyscall_printf("DMA %s words=%u nodes=[%s] prePolls=%u dmaPolls=%u statAtDmaDone=%08x idlePolls=%u statIdle=%08x\n",
                      tag, s_n, s_nb ? desc : "single", pre, dp, (unsigned)statDone, ip, (unsigned)statIdle);
}

/* CPU send of a sequence of commands; lens[] gives words per command.
   Waits GPUSTAT 26+28 before every command and at the end. */
static void cpuRun(const char *tag, const unsigned *lens, unsigned ncmd) {
    unsigned idx = 1, maxp = 0;
    for (unsigned c = 0; c < ncmd; c++) {
        unsigned p = waitIdle();
        if (p > maxp) maxp = p;
        for (unsigned i = 0; i < lens[c]; i++) GPU_DATA = s_chain[idx++];
    }
    unsigned p = waitIdle();
    ramsyscall_printf("CPU %s words=%u cmds=%u maxIdlePolls=%u finalIdlePolls=%u\n",
                      tag, idx - 1, ncmd, maxp, p);
}

static void fillBg(int16_t x, int16_t y, int16_t w, int16_t h) {
    /* GP0(02h) fill, R8=0x80 -> VRAM 0x0010 */
    waitIdle();
    GPU_DATA = 0x02000080u;
    GPU_DATA = ((uint32_t)(uint16_t)y << 16) | (uint32_t)(uint16_t)x;
    GPU_DATA = ((uint32_t)(uint16_t)h << 16) | (uint32_t)(uint16_t)w;
    waitIdle();
}

/* Verify every pixel of a region equals v. Returns mismatch count. */
static unsigned verifyRegion(const char *tag, int16_t x, int16_t y, int16_t w, int16_t h,
                             uint16_t v) {
    unsigned bad = 0, first = 0xffffffffu;
    for (int16_t yy = y; yy < y + h; yy++) {
        rasterReadStrip(x, yy, w, s_row);
        for (int i = 0; i < w; i++) {
            if (s_row[i] != v) {
                if (bad == 0) first = ((unsigned)yy << 16) | (unsigned)(x + i) | 0;
                bad++;
            }
        }
    }
    ramsyscall_printf("FILL %s region=(%d,%d %dx%d) want=%04x mismatches=%u first@y=%u,x=%u\n",
                      tag, x, y, w, h, (unsigned)v, bad, first >> 16, first & 0xffff);
    return bad;
}

/* Measure bit15 inside quad [x0,x1) x [y0,y1) and a 2px ring around it. */
static unsigned measureQuad(const char *tag, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    unsigned in15 = 0, inTotal = 0, inMasked = 0, inBg = 0, inOther = 0, ring15 = 0;
    unsigned otherVal = 0;
    int16_t rx = x0 - 2, rw = (x1 - x0) + 4;
    /* row RLE of (count,firstX,lastX) */
    int runStart = -1; unsigned runCnt = 0, runFx = 0, runLx = 0;
    ramsyscall_printf("ROWS %s:", tag);
    for (int16_t yy = y0 - 2; yy < y1 + 2; yy++) {
        rasterReadStrip(rx, yy, rw, s_row);
        unsigned rc = 0, fx = 0xffff, lx = 0xffff;
        for (int i = 0; i < rw; i++) {
            int16_t xx = rx + i;
            uint16_t v = s_row[i];
            int inside = xx >= x0 && xx < x1 && yy >= y0 && yy < y1;
            if (inside) {
                inTotal++;
                if (v & 0x8000u) { in15++; rc++; if (fx == 0xffff) fx = xx; lx = xx; }
                if (v == BG_MASKED) inMasked++;
                else if (v == BG) inBg++;
                else { if (inOther == 0) otherVal = v; inOther++; }
            } else if (v & 0x8000u) {
                ring15++;
            }
        }
        if (yy >= y0 && yy < y1) {
            if (runStart >= 0 && rc == runCnt && fx == runFx && lx == runLx) continue;
            if (runStart >= 0) ramsyscall_printf(" [y%d..%d c=%u x%u..%u]", runStart, yy - 1, runCnt, runFx, runLx);
            runStart = yy; runCnt = rc; runFx = fx; runLx = lx;
        }
    }
    if (runStart >= 0) ramsyscall_printf(" [y%d..%d c=%u x%u..%u]", runStart, y1 - 1, runCnt, runFx, runLx);
    ramsyscall_printf("\n");
    ramsyscall_printf("RES %s bit15=%u/%u eq8010=%u eq0010=%u other=%u (first other=%04x) ring_bit15=%u\n",
                      tag, in15, inTotal, inMasked, inBg, inOther, otherVal, ring15);
    return in15 == inTotal && ring15 == 0 && inMasked == inTotal ? 1 : 0;
}

static void putQuadSemi(uint32_t col, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    put(0x2a000000u | (col & 0xffffffu));
    put(((uint32_t)(uint16_t)y0 << 16) | (uint16_t)x0);
    put(((uint32_t)(uint16_t)y0 << 16) | (uint16_t)x1);
    put(((uint32_t)(uint16_t)y1 << 16) | (uint16_t)x0);
    put(((uint32_t)(uint16_t)y1 << 16) | (uint16_t)x1);
}

/* Arms A..D layout */
#define QX0 64
#define QY0 16
#define FX 48
#define FY 0
#define FW 288
#define FH 232

static int maskArm(const char *tag, int16_t w, int16_t h, int mode) {
    rasterReset();
    rasterSetAbr(1);
    fillBg(FX, FY, FW, FH);
    unsigned bad = verifyRegion(tag, FX, FY, FW, FH, BG);
    chainBegin();
    put(0xe6000001u);
    putQuadSemi(0x000000u, QX0, QY0, QX0 + w, QY0 + h);
    if (mode == 2) {
        put(0x01000000u);
        put(0x80000000u);
        put((500u << 16) | 1000u);
        put((500u << 16) | 1002u);
        put((1u << 16) | 1u);
    }
    put(0xe6000000u);
    if (mode == 1) {
        static const unsigned lens[] = {1, 5, 1};
        cpuRun(tag, lens, 3);
    } else {
        chainRun(tag);
    }
    uint32_t stat = GPU_STATUS;
    ramsyscall_printf("STAT %s gpustat=%08x (bit11 setmask=%u bit12 checkmask=%u)\n", tag,
                      (unsigned)stat, (unsigned)((stat >> 11) & 1), (unsigned)((stat >> 12) & 1));
    int ok = measureQuad(tag, QX0, QY0, QX0 + w, QY0 + h);
    rasterSetAbr(0);
    return bad == 0 && ok;
}

/* Arm E layout: 16x16 4bpp texture at (768,0), CLUT at (768,256) */
#define EX0 400
#define EY0 32
#define ESZ 16
#define TPAGE_FIELD ((768 / 64) | (1u << 5))   /* tx=12, ABR=1, 4bpp */
#define CLUT_FIELD ((768 / 16) | (256u << 6))
#define TEXEL_FOG 0x211fu
#define HOLE_FOG 0x2118u

static void uploadTex(void) {
    waitIdle();
    GPU_DATA = 0xa0000000u;
    GPU_DATA = (0u << 16) | 768u;
    GPU_DATA = (16u << 16) | 4u;
    for (int y = 0; y < 16; y++) {
        GPU_DATA = 0x00000000u;   /* u0-7: index 0 (hole) */
        GPU_DATA = 0x11111111u;   /* u8-15: index 1 (red) */
    }
    waitIdle();
    GPU_DATA = 0xa0000000u;
    GPU_DATA = (256u << 16) | 768u;
    GPU_DATA = (1u << 16) | 16u;
    GPU_DATA = 0x001f0000u;       /* [0]=0x0000 [1]=0x001f */
    for (int i = 1; i < 8; i++) GPU_DATA = 0x7fff7fffu;
    waitIdle();
    rasterReadStrip(768, 256, 2, s_row);
    unsigned c0 = s_row[0], c1 = s_row[1];
    rasterReadStrip(768, 5, 4, s_row);
    ramsyscall_printf("TEX clut0=%04x clut1=%04x texrow5=%04x %04x %04x %04x\n", c0, c1,
                      s_row[0], s_row[1], s_row[2], s_row[3]);
}

/* variant knobs for arm E (0 = baseline) */
static unsigned s_texCmd = 0x2d000000u;  /* or 0x2c808080 */
static unsigned s_tpExtra = 0;           /* tpage bit 9 = dither */
static int s_fogGouraud = 0;
static int s_sz = ESZ;                    /* screen size of quad */
static void putStage1(void) {
    put(0xe6000001u);
    putQuadSemi(0x000000u, EX0, EY0, EX0 + s_sz, EY0 + s_sz);
    put(0xe6000000u);
    put(s_texCmd);
    put(((uint32_t)EY0 << 16) | EX0);
    put(((uint32_t)CLUT_FIELD << 16) | (0u << 8) | 0u);
    put(((uint32_t)EY0 << 16) | (EX0 + s_sz));
    put(((uint32_t)(TPAGE_FIELD | s_tpExtra) << 16) | (0u << 8) | 16u);
    put(((uint32_t)(EY0 + s_sz) << 16) | EX0);
    put((16u << 8) | 0u);
    put(((uint32_t)(EY0 + s_sz) << 16) | (EX0 + s_sz));
    put((16u << 8) | 16u);
}
static void putStage2(void) {
    put(0xe6000002u);
    if (s_fogGouraud) {
        put(0x3a404040u); put(((uint32_t)EY0 << 16) | EX0);
        put(0x404040u);   put(((uint32_t)EY0 << 16) | (EX0 + s_sz));
        put(0x404040u);   put(((uint32_t)(EY0 + s_sz) << 16) | EX0);
        put(0x404040u);   put(((uint32_t)(EY0 + s_sz) << 16) | (EX0 + s_sz));
    } else {
        putQuadSemi(0x404040u, EX0, EY0, EX0 + s_sz, EY0 + s_sz);
    }
    put(0xe6000000u);
}

/* Classify the 16x16 quad: columns u<8 holes, u>=8 texels. */
static int measureE(const char *tag, uint16_t wantHole, uint16_t wantTex) {
    unsigned h15 = 0, hOk = 0, t15 = 0, tOk = 0, hOther = 0, tOther = 0;
    uint16_t hv = 0, tv = 0;
    unsigned tot = (unsigned)s_sz * (unsigned)s_sz / 2;
    unsigned nP = 0, nTF = 0, nHF = 0, nT = 0, nB = 0, nO = 0; uint16_t oV = 0;
    for (int y = 0; y < s_sz; y++) {
        rasterReadStrip(EX0, EY0 + y, s_sz, s_row);
        for (int x = 0; x < s_sz; x++) {
            uint16_t v = s_row[x];
            if (v == 0x8010u) nP++; else if (v == TEXEL_FOG) nTF++; else if (v == HOLE_FOG) nHF++;
            else if (v == 0x001fu) nT++; else if (v == 0x0010u) nB++; else { if (!nO) oV = v; nO++; }
            if (x < s_sz / 2) {
                if (v & 0x8000u) h15++;
                if (v == wantHole) hOk++; else { if (!hOther) hv = v; hOther++; }
            } else {
                if (v & 0x8000u) t15++;
                if (v == wantTex) tOk++; else { if (!tOther) tv = v; tOther++; }
            }
        }
    }
    ramsyscall_printf("HIST %s total=%u hole_protected(8010)=%u texel_fog(211f)=%u HOLE_FOGGED(2118)=%u texel_nofog(001f)=%u bg_nomask(0010)=%u other=%u(first %04x)\n",
                      tag, tot * 2, nP, nTF, nHF, nT, nB, nO, oV);
    int hx = EX0 + 3 * s_sz / 16, tx = EX0 + 12 * s_sz / 16, py = EY0 + s_sz / 2;
    uint16_t hole = rasterReadPixel(hx, py);
    uint16_t tex = rasterReadPixel(tx, py);
    ramsyscall_printf("PIX %s hole(%d,%d)=%04x r=%u g=%u b=%u m=%u | texel(%d,%d)=%04x r=%u g=%u b=%u m=%u\n",
                      tag, hx, py, hole, hole & 31, (hole >> 5) & 31, (hole >> 10) & 31, hole >> 15,
                      tx, py, tex, tex & 31, (tex >> 5) & 31, (tex >> 10) & 31, tex >> 15);
    ramsyscall_printf("RES %s holes: bit15=%u/%u eq%04x=%u firstOther=%04x | texels: bit15=%u/%u eq%04x=%u firstOther=%04x\n",
                      tag, h15, tot, wantHole, hOk, hv, t15, tot, wantTex, tOk, tv);
    /* value-based pass: every pixel is a protected hole or the wanted texel
       (column split is exact only at 1:1 scale) */
    return nP + (wantTex == TEXEL_FOG ? nTF : nT) == tot * 2 && wantHole == BG_MASKED;
}

static void eSetup(const char *tag) {
    rasterReset();
    rasterSetAbr(1);
    uploadTex();
    fillBg(384, 0, 288, 256);
    verifyRegion(tag, EX0 - 4, EY0 - 4, s_sz + 8, s_sz + 8, BG);
}

/* Background for stage-2-only: holes 0x8010, texels 0x001f (as after an
   in-order stage 1). */
static void prepStage2Bg(void) {
    waitIdle();
    GPU_DATA = 0xa0000000u;
    GPU_DATA = ((uint32_t)EY0 << 16) | EX0;
    GPU_DATA = ((uint32_t)ESZ << 16) | ESZ;
    for (int y = 0; y < ESZ; y++)
        for (int i = 0; i < ESZ / 2; i++)
            GPU_DATA = i < 4 ? 0x80108010u : 0x001f001fu;
    waitIdle();
}
)

CESTER_BEFORE_ALL(gpu_dma_node,
    s_ie = enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    rasterFullReset();
    ramsyscall_printf("\n=== gpu_dma_node: E6 ordering vs queued polygons ===\n");
    ramsyscall_printf("Grep '^RES|^PIX|^DMA|^CPU|^FILL|^ROWS|^STAT'\n");
)

CESTER_AFTER_ALL(gpu_dma_node,
    if (s_ie) leaveCriticalSection();
)

CESTER_TEST(armA_chain_256x200, gpu_dma_node, cester_assert_int_eq(1, maskArm("A_chain_256x200", 256, 200, 0)); )
CESTER_TEST(armB_chain_4x4, gpu_dma_node, cester_assert_int_eq(1, maskArm("B_chain_4x4", 4, 4, 0)); )
CESTER_TEST(armC_cpu_256x200, gpu_dma_node, cester_assert_int_eq(1, maskArm("C_cpu_256x200", 256, 200, 1)); )
CESTER_TEST(armD_chain_flush_copy, gpu_dma_node, cester_assert_int_eq(1, maskArm("D_chain_flush_copy", 256, 200, 2)); )

CESTER_TEST(armE_chain_full, gpu_dma_node,
    eSetup("E_chain_full");
    chainBegin(); putStage1(); putStage2();
    chainRun("E_chain_full");
    cester_assert_int_eq(1, measureE("E_chain_full", BG_MASKED, TEXEL_FOG));
)
CESTER_TEST(armE_cpu_full, gpu_dma_node,
    eSetup("E_cpu_full");
    chainBegin(); putStage1(); putStage2();
    static const unsigned lens[] = {1, 5, 1, 9, 1, 5, 1};
    cpuRun("E_cpu_full", lens, 7);
    cester_assert_int_eq(1, measureE("E_cpu_full", BG_MASKED, TEXEL_FOG));
)
CESTER_TEST(armE_s1_chain, gpu_dma_node,
    eSetup("E_s1_chain");
    chainBegin(); putStage1();
    chainRun("E_s1_chain");
    cester_assert_int_eq(1, measureE("E_s1_chain", BG_MASKED, 0x001fu));
)
CESTER_TEST(armE_s2_chain, gpu_dma_node,
    eSetup("E_s2_chain");
    prepStage2Bg();
    measureE("E_s2_pre", BG_MASKED, 0x001fu);
    chainBegin(); putStage2();
    chainRun("E_s2_chain");
    cester_assert_int_eq(1, measureE("E_s2_chain", BG_MASKED, TEXEL_FOG));
)
CESTER_TEST(armE_split_chain, gpu_dma_node,
    eSetup("E_split_chain");
    chainBegin(); putStage1();
    chainRun("E_split_s1");
    chainBegin(); putStage2();
    chainRun("E_split_s2");
    cester_assert_int_eq(1, measureE("E_split_chain", BG_MASKED, TEXEL_FOG));
)

#define E_VARIANT(name, setup, want)                                       \
CESTER_TEST(name, gpu_dma_node,                                                 \
    s_texCmd = 0x2d000000u; s_tpExtra = 0; s_fogGouraud = 0; s_sz = ESZ;   \
    setup;                                                                 \
    eSetup(#name);                                                         \
    if (s_tpExtra & 0x200) sendGPUData(0xe1000600u | (1u << 5));           \
    chainBegin(); putStage1(); putStage2();                                \
    chainRun(#name);                                                       \
    int ok = measureE(#name, BG_MASKED, TEXEL_FOG);                        \
    if (want) cester_assert_int_eq(1, ok);                                 \
    s_sz = ESZ;                                                            \
)
E_VARIANT(armE_v_mod2c, s_texCmd = 0x2c808080u, 1)
E_VARIANT(armE_v_dither, s_tpExtra = 0x200, 1)
E_VARIANT(armE_v_gouraudfog, s_fogGouraud = 1, 1)
E_VARIANT(armE_v_big160, s_sz = 160, 0)

CESTER_BODY(
/* Arm G: FIFO overrun detector. One node: E6=1, semi quad (w x h), then
   16 GP0(68h) 1x1 dots (2 words each, R5 = i+1) at (600+i, 300), E6=0.
   Prints which dots landed and their values. */
static int dotArm(const char *tag, int16_t w, int16_t h, int split) {
    rasterReset();
    rasterSetAbr(1);
    fillBg(FX, FY, FW, FH);
    fillBg(592, 296, 32, 8);
    chainBegin();
    put(0xe6000001u);
    putQuadSemi(0x000000u, QX0, QY0, QX0 + w, QY0 + h);
    if (split) nodeBreak();
    for (unsigned i = 0; i < 16; i++) {
        put(0x68000000u | (((i + 1) & 31) << 3));
        put((300u << 16) | (600u + i));
        if (split && (i & 3) == 3) nodeBreak();
    }
    put(0xe6000000u);
    chainRun(tag);
    rasterReadStrip(600, 300, 16, s_row);
    unsigned ok = 0;
    char map[17];
    for (unsigned i = 0; i < 16; i++) {
        int good = s_row[i] == (0x8000u | (i + 1));
        ok += good;
        map[i] = good ? '1' : (s_row[i] == BG ? '.' : '?');
    }
    map[16] = 0;
    ramsyscall_printf("DOTS %s ok=%u/16 map=%s vals=", tag, ok, map);
    for (unsigned i = 0; i < 16; i++) ramsyscall_printf("%04x ", s_row[i]);
    ramsyscall_printf("\n");
    measureQuad(tag, QX0, QY0, QX0 + w, QY0 + h);
    uint32_t st = GPU_STATUS;
    ramsyscall_printf("STAT %s final bit11=%u bit12=%u\n", tag, (unsigned)((st >> 11) & 1), (unsigned)((st >> 12) & 1));
    rasterSetAbr(0);
    return ok == 16;
}
)

CESTER_MAYBE_TEST(armG_dots_after_4x4, gpu_dma_node, cester_assert_int_eq(0, dotArm("G_dots_after_4x4", 4, 4, 0)); )
CESTER_MAYBE_TEST(armG_dots_after_256x200, gpu_dma_node, cester_assert_int_eq(0, dotArm("G_dots_after_256x200", 256, 200, 0)); )
CESTER_TEST(armG_dots_after_256x200_split, gpu_dma_node, cester_assert_int_eq(1, dotArm("G_dots_after_256x200_split", 256, 200, 1)); )

/* Arm H: the 26-word gouraud-fog face split into nodes [7][9][10], and the
   23-word face at 160x160 split [7][9][7]. */
CESTER_TEST(armH_gouraudfog_split, gpu_dma_node,
    s_texCmd = 0x2d000000u; s_tpExtra = 0; s_fogGouraud = 1; s_sz = ESZ;
    eSetup("H_gouraudfog_split");
    chainBegin();
    put(0xe6000001u); putQuadSemi(0, EX0, EY0, EX0 + s_sz, EY0 + s_sz); put(0xe6000000u);
    nodeBreak();
    { unsigned m = s_n; putStage1(); /* rewind: stage1 re-emits mask part, drop it */
      unsigned n1 = s_n - m; for (unsigned i = 0; i < 9; i++) s_chain[1 + m + i] = s_chain[1 + m + 7 + i]; s_n = m + 9; (void)n1; }
    nodeBreak();
    putStage2();
    chainRun("H_gouraudfog_split");
    measureE("H_gouraudfog_split", BG_MASKED, TEXEL_FOG);
    s_fogGouraud = 0;
)
CESTER_TEST(armH_big160_split, gpu_dma_node,
    s_texCmd = 0x2d000000u; s_tpExtra = 0; s_fogGouraud = 0; s_sz = 160;
    eSetup("H_big160_split");
    chainBegin();
    put(0xe6000001u); putQuadSemi(0, EX0, EY0, EX0 + s_sz, EY0 + s_sz); put(0xe6000000u);
    nodeBreak();
    { unsigned m = s_n; putStage1();
      for (unsigned i = 0; i < 9; i++) s_chain[1 + m + i] = s_chain[1 + m + 7 + i]; s_n = m + 9; }
    nodeBreak();
    putStage2();
    chainRun("H_big160_split");
    measureE("H_big160_split", BG_MASKED, TEXEL_FOG);
    s_sz = ESZ;
)

CESTER_BODY(
/* Arm S: single node = E6=1, 256x200 semi quad, k dots, E6=0 (7+2k words). */
static int dotSweep(int k) {
    char tag[24];
    tag[0] = 'S'; tag[1] = '_'; tag[2] = 'k'; tag[3] = '0' + k / 10; tag[4] = '0' + k % 10; tag[5] = 0;
    rasterReset();
    rasterSetAbr(1);
    fillBg(FX, FY, FW, FH);
    fillBg(592, 296, 32, 8);
    chainBegin();
    put(0xe6000001u);
    putQuadSemi(0x000000u, QX0, QY0, QX0 + 256, QY0 + 200);
    for (int i = 0; i < k; i++) {
        put(0x68000000u | (((i + 1) & 31) << 3));
        put((300u << 16) | (600u + i));
    }
    put(0xe6000000u);
    chainRun(tag);
    rasterReadStrip(600, 300, 16, s_row);
    char map[17];
    unsigned ok = 0;
    for (int i = 0; i < 16; i++) {
        int good = s_row[i] == (0x8000u | (i + 1));
        ok += good;
        map[i] = i >= k ? (s_row[i] == BG ? '-' : '!') : good ? '1' : (s_row[i] == BG ? '.' : '?');
    }
    map[16] = 0;
    ramsyscall_printf("DOTS %s words=%d ok=%u/%d map=%s\n", tag, 7 + 2 * k, ok, k, map);
    int quad = measureQuad(tag, QX0, QY0, QX0 + 256, QY0 + 200);
    return quad && ok == (unsigned)k;
}

/* Arm P: GPU busy when the face node starts. Node 1 = opaque 256x200 quad
   (0x28) elsewhere; then the 23-word face (16x16), single node or split. */
static int busyFace(const char *tag, int split) {
    s_texCmd = 0x2d000000u; s_tpExtra = 0; s_fogGouraud = 0; s_sz = ESZ;
    eSetup(tag);
    fillBg(FX, FY, FW, FH);
    chainBegin();
    put(0x28000000u | 0x00f800u);
    put(((uint32_t)QY0 << 16) | QX0);
    put(((uint32_t)QY0 << 16) | (QX0 + 256));
    put(((uint32_t)(QY0 + 200) << 16) | QX0);
    put(((uint32_t)(QY0 + 200) << 16) | (QX0 + 256));
    nodeBreak();
    put(0xe6000001u); putQuadSemi(0, EX0, EY0, EX0 + s_sz, EY0 + s_sz); put(0xe6000000u);
    if (split) nodeBreak();
    { unsigned m = s_n; putStage1();
      for (unsigned i = 0; i < 9; i++) s_chain[1 + m + i] = s_chain[1 + m + 7 + i]; s_n = m + 9; }
    if (split) nodeBreak();
    putStage2();
    chainRun(tag);
    int ok = measureE(tag, BG_MASKED, TEXEL_FOG);
    uint16_t big = rasterReadPixel(QX0 + 100, QY0 + 100);
    ramsyscall_printf("BUSY %s bigquad_px=%04x (want 03e0)\n", tag, big);
    return ok && big == 0x03e0u;
}
)

/* up to 17 words: correct everywhere */
CESTER_TEST(armS_sweep_short, gpu_dma_node,
    for (int k = 2; k <= 5; k++) cester_assert_int_eq(1, dotSweep(k));
)
/* 19 and 21 words: the quad is cut short on hardware, the dots land */
CESTER_TEST(armS_sweep_edge, gpu_dma_node,
    dotSweep(6);
    dotSweep(7);
)
/* 23 words and up: corrupted on hardware */
CESTER_MAYBE_TEST(armS_sweep_long, gpu_dma_node,
    static const int ks[] = {8, 9, 10, 12};
    for (unsigned i = 0; i < sizeof(ks) / sizeof(ks[0]); i++) cester_assert_int_eq(0, dotSweep(ks[i]));
)
CESTER_MAYBE_TEST(armP_busy_face_single, gpu_dma_node, cester_assert_int_eq(0, busyFace("P_busy_face_single", 0)); )
CESTER_TEST(armP_busy_face_split, gpu_dma_node, cester_assert_int_eq(1, busyFace("P_busy_face_split", 1)); )
