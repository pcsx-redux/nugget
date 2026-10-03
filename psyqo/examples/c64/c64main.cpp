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

// Runs the C64 one guest frame per PS1 frame and draws the VIC-II text screen
// with GPU primitives. Rendering follows the raster: border and background are
// rectangles over runs of lines with the same colour, and each text row is drawn
// as 8-pixel-wide sprites from a 4bpp copy of the character set, one CLUT per
// foreground colour. A text row is split wherever the character set changes
// mid-row, so a raster split there is drawn on the line it happened.

#include <stdint.h>


#include "c64.hh"
#include "../m6502bench/m6502jit.hh"
#include "common/hardware/counters.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/gpu.hh"
#include "psyqo/scene.hh"
#include "roms.h"

namespace {

// Pepto's palette.
static const uint32_t c_palette[16] = {
    0x000000, 0xffffff, 0x68372b, 0x70a4b2, 0x6f3d86, 0x588d43, 0x352879, 0xb8c76f,
    0x6f4f25, 0x433900, 0x9a6759, 0x444444, 0x6c6c6c, 0x9ad284, 0x6c5eb5, 0x959595,
};
static inline uint32_t bgr24(uint32_t rgb) {
    return ((rgb >> 16) & 0xff) | (rgb & 0xff00) | ((rgb & 0xff) << 16);
}
static inline uint16_t bgr15(uint32_t rgb) {
    uint16_t v = (((rgb >> 19) & 0x1f)) | (((rgb >> 11) & 0x1f) << 5) | (((rgb >> 3) & 0x1f) << 10);
    return v ? v : 0x8000;
}

// VRAM layout: one 4bpp texture page at x=640 holds four character sets of
// 256 glyphs, 32 by 8 glyphs each, stacked vertically. Slots 0 and 1 are the
// ROM upper and lower case sets, 2 and 3 take RAM character sets per frame.
static constexpr uint32_t c_texX = 640;
static constexpr uint32_t c_clutY = 256;
static constexpr uint32_t c_tpage = 0xe1000000 | (c_texX / 64) | (1 << 10);

// A DMA chain node: up to 12 payload words.
struct Node {
    uintptr_t head;
    uint32_t words[12];
    uint32_t count;
    size_t getActualFragmentSize() const { return count; }
};

static constexpr uint32_t c_maxRectNodes = 160;
static constexpr uint32_t c_maxSpriteNodes = 400;
// Double-buffered: frame N's chain is sent at the flip and drawn while frame
// N+1 is being emulated, so N+1 builds into the other set.
static Node s_rectBuf[2][c_maxRectNodes];
static Node s_spriteBuf[2][c_maxSpriteNodes];
static Node* s_rectNodes;
static Node* s_spriteNodes;
static uint32_t s_rectCount, s_spriteCount;

// A RAM character set goes to VRAM inside the chain, as 16 CPU->VRAM copies of
// 4 texture rows each, so it never needs a blocking upload mid-frame.
struct UploadNode {
    uintptr_t head;
    uint32_t words[3 + 128];
    uint32_t count;
    size_t getActualFragmentSize() const { return count; }
};
static UploadNode s_uploads[2][16];
static bool s_uploadPending;
static uint32_t s_parity;
static uint32_t s_dropped;

static inline uint32_t* reserve(Node* nodes, uint32_t& count, uint32_t max, uint32_t words) {
    if (count == 0 || nodes[count - 1].count + words > 12) {
        if (count == max) {
            s_dropped++;
            return nullptr;
        }
        nodes[count++].count = 0;
    }
    Node& n = nodes[count - 1];
    uint32_t* p = n.words + n.count;
    n.count += words;
    return p;
}

static void emitRect(uint32_t y, uint32_t h, uint32_t color) {
    uint32_t* p = reserve(s_rectNodes, s_rectCount, c_maxRectNodes, 3);
    if (!p) return;
    p[0] = 0x60000000 | bgr24(c_palette[color & 15]);
    p[1] = y << 16;
    p[2] = (h << 16) | 320;
}

// Border/background runs, merged while contiguous and the same colour.
static int32_t s_runY = -1;
static uint32_t s_runH, s_runColor;
static void runRect(uint32_t y, uint32_t h, uint32_t color) {
    if (s_runY >= 0 && color == s_runColor && (int32_t)y == s_runY + (int32_t)s_runH) {
        s_runH += h;
        return;
    }
    if (s_runY >= 0) emitRect(s_runY, s_runH, s_runColor);
    s_runY = y;
    s_runH = h;
    s_runColor = color;
}
static void runFlush() {
    if (s_runY >= 0) emitRect(s_runY, s_runH, s_runColor);
    s_runY = -1;
}

// Raster lines [a, b) in one colour. Raster 31..262 shows as PS1 lines 0..231
// and raster 0..7 as 232..239; the 23 lines of vertical blank in between are
// not shown.
static void rasterRect(uint32_t a, uint32_t b, uint32_t color) {
    uint32_t s0 = a < 31 ? 31 : a;
    if (s0 < b) runRect(s0 - 31, b - s0, color);
    uint32_t e1 = b < 8 ? b : 8;
    if (a < e1) runRect(232 + a, e1 - a, color);
}

// Character set texture slots.
static uint32_t s_expand[256];
static uint32_t s_slotImage[64 * 32];
static uint32_t s_slotKey[4];
static uint32_t s_slotFrame[4];
// One bit per glyph that has no set pixel; those are never drawn.
static uint32_t s_blank[4][8];

static void scanBlank(uint32_t slot, const uint8_t* src) {
    for (uint32_t c = 0; c < 256; c++) {
        uint32_t any = 0;
        for (uint32_t r = 0; r < 8; r++) any |= src[c * 8 + r];
        if (any) {
            s_blank[slot][c >> 5] &= ~(1u << (c & 31));
        } else {
            s_blank[slot][c >> 5] |= 1u << (c & 31);
        }
    }
}

// Which slot holds the character set the VIC-II is pointing at.
static uint8_t s_ramCharset[2048];
static int32_t charsetSlot(const c64::Span& lr) {
    const uint32_t base = lr.bank << 14;
    const uint32_t off = (lr.d018 & 0x0e) << 10;
    if (!(base & 0x4000) && (off & 0x3000) == 0x1000) return (off >> 11) & 1;
    const uint32_t key = base | off | 0x80000000;
    // One RAM set per frame, in the slot matching the frame's parity: frame N's
    // upload data is not touched again until frame N+2.
    const uint32_t s = 2 + s_parity;
    if (s_slotKey[s] == key && s_slotFrame[s] == c64::g_frame) return s;
    if (s_uploadPending) return s;
    for (uint32_t i = 0; i < 2048; i++) s_ramCharset[i] = c64::ramByte(base | (off + i));
    scanBlank(s, s_ramCharset);
    for (uint32_t n = 0; n < 16; n++) {
        UploadNode& u = s_uploads[s_parity][n];
        u.words[0] = 0xa0000000;
        u.words[1] = ((s * 64 + n * 4) << 16) | c_texX;
        u.words[2] = (4 << 16) | 64;
        // Texture rows n*4 .. n*4+3, 32 words each, laid out like s_slotImage.
        for (uint32_t k = 0; k < 128; k++) {
            const uint32_t row = n * 4 + (k >> 5);
            const uint32_t c = ((row >> 3) << 5) | (k & 31);
            u.words[3 + k] = s_expand[s_ramCharset[c * 8 + (row & 7)]];
        }
        u.count = 3 + 128;
    }
    s_uploadPending = true;
    s_slotKey[s] = key;
    s_slotFrame[s] = c64::g_frame;
    return s;
}

// The text row being drawn and the pending sprite segment for it.
static const uint8_t* s_rowCodes;
static const uint8_t* s_rowColors;
static int32_t s_segY = -1;
static uint32_t s_segRc, s_segH;
static int32_t s_segSlot;

static void segFlush() {
    if (s_segY < 0) return;
    const uint32_t v0 = s_segSlot * 64 + s_segRc;
    const uint32_t* blank = s_blank[s_segSlot];
    // Rows are mostly spaces: test four codes per load against a blank glyph.
    const uint32_t* words = reinterpret_cast<const uint32_t*>(s_rowCodes);
    const bool spaceBlank = (blank[1] & 1) != 0;
    for (uint32_t i = 0; i < 40; i++) {
        if (spaceBlank && (i & 3) == 0 && words[i >> 2] == 0x20202020) {
            i += 3;
            continue;
        }
        const uint32_t code = s_rowCodes[i];
        if ((blank[code >> 5] >> (code & 31)) & 1) continue;
        uint32_t* p = reserve(s_spriteNodes, s_spriteCount, c_maxSpriteNodes, 4);
        if (!p) break;
        const uint32_t clut = (c_clutY + s_rowColors[i]) << 6 | (c_texX >> 4);
        p[0] = 0x65808080;
        p[1] = (s_segY << 16) | (i * 8);
        p[2] = (clut << 16) | ((v0 + (code >> 5) * 8) << 8) | ((code & 31) * 8);
        p[3] = (s_segH << 16) | 8;
    }
    s_segY = -1;
}

static inline uint32_t windowTop(uint8_t d011) { return (d011 & 0x08) ? 0x33 : 0x37; }
static inline uint32_t windowBottom(uint8_t d011) { return (d011 & 0x08) ? 0xfb : 0xf7; }

static void drawFrame() {
    const uint32_t spans = c64::g_spanCount;
    const c64::Span* sp = c64::g_spans;
    auto spanEnd = [&](uint32_t i) -> uint32_t { return i + 1 < spans ? sp[i + 1].line : c64::c_linesPerFrame; };

    // Border and background.
    for (uint32_t i = 0; i < spans; i++) {
        const uint32_t a = sp[i].line, b = spanEnd(i);
        const uint8_t d011 = sp[i].d011;
        const uint32_t border = sp[i].d020 & 15, bg = sp[i].d021 & 15;
        if (!(d011 & 0x10)) {
            rasterRect(a, b, border);
            continue;
        }
        const uint32_t top = windowTop(d011), bottom = windowBottom(d011);
        if (a < top) rasterRect(a, b < top ? b : top, border);
        const uint32_t wa = a > top ? a : top, wb = b < bottom ? b : bottom;
        if (wa < wb) rasterRect(wa, wb, bg);
        if (b > bottom) rasterRect(a > bottom ? a : bottom, b, border);
    }

    // Text rows, split where a span boundary changes the character set or mode.
    uint32_t si = 0;
    for (uint32_t r = 0; r < c64::g_rowCount; r++) {
        const uint32_t rowStart = c64::g_rowLine[r];
        uint32_t rowEnd = rowStart + 8;
        if (r + 1 < c64::g_rowCount && c64::g_rowLine[r + 1] < rowEnd) rowEnd = c64::g_rowLine[r + 1];
        s_rowCodes = c64::g_rowCodes[r];
        s_rowColors = c64::g_rowColors[r];
        while (si + 1 < spans && sp[si + 1].line <= rowStart) si++;
        for (uint32_t k = si; k < spans && sp[k].line < rowEnd; k++) {
            const c64::Span& s = sp[k];
            uint32_t a = s.line > rowStart ? s.line : rowStart;
            uint32_t b = spanEnd(k) < rowEnd ? spanEnd(k) : rowEnd;
            const uint8_t d011 = s.d011;
            const bool text = (d011 & 0x10) && !(d011 & 0x60) && !(s.d016 & 0x10);
            const uint32_t top = windowTop(d011), bottom = windowBottom(d011);
            if (a < top) a = top;
            if (b > bottom) b = bottom;
            if (!text || a >= b || a < 31) {
                segFlush();
                continue;
            }
            const int32_t slot = charsetSlot(s);
            const int32_t y = a - 31;
            if (s_segY >= 0 && (slot != s_segSlot || y != s_segY + (int32_t)s_segH)) segFlush();
            if (s_segY < 0) {
                s_segY = y;
                s_segRc = a - rowStart;
                s_segH = 0;
                s_segSlot = slot;
            }
            s_segH += b - a;
        }
        segFlush();
    }
}

static void renderBegin() {
    s_parity ^= 1;
    s_rectNodes = s_rectBuf[s_parity];
    s_spriteNodes = s_spriteBuf[s_parity];
    s_uploadPending = false;
    s_rectCount = s_spriteCount = 0;
    s_runY = -1;
    s_segY = -1;
    // Texture cache flush, since a RAM character set may have been uploaded
    // ahead of the sprites, then the texture page.
    uint32_t* p = reserve(s_spriteNodes, s_spriteCount, c_maxSpriteNodes, 2);
    p[0] = 0x01000000;
    p[1] = c_tpage;
}

static void renderEnd(psyqo::GPU& gpu) {
    runFlush();
    if (s_uploadPending) {
        for (uint32_t n = 0; n < 16; n++) gpu.chain(s_uploads[s_parity][n]);
    }
    for (uint32_t i = 0; i < s_rectCount; i++) gpu.chain(s_rectNodes[i]);
    for (uint32_t i = 0; i < s_spriteCount; i++) gpu.chain(s_spriteNodes[i]);
    // Sent at the flip, drawn while the next frame is emulated.
}

class C64App final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class C64Scene final : public psyqo::Scene {
    void start(StartReason) override;
    void frame() override;
};

C64App g_app;
C64Scene g_scene;

static uint32_t hblanks() { return COUNTERS[1].value; }


// Per-report-window timing, in hblanks (63.6 us each on NTSC).
static uint32_t s_sumBuild, s_sumEmu, s_sumTotal, s_maxTotal, s_frames, s_lastVsync, s_vsyncs;
static bool s_reportedReady;
// Which core each 300-frame report window ran. Boot runs translated; after
// READY. the windows alternate, so the same run carries an A/B.
static bool s_jit = true;
static uint32_t s_window;

static void setCore(bool jit) {
    s_jit = jit;
    if (jit) {
        // The interpreter windows do not watch for self-modifying code, so drop
        // everything translated before running translated code again.
        m6502jit::invalidateRange(0, 0x10000);
        c64::g_core = m6502jit::run;
        c64::g_bankHook = m6502jit::invalidateRange;
    } else {
        c64::g_core = nullptr;
        c64::g_bankHook = nullptr;
    }
}

static void dumpScreen() {
    const uint32_t screen = (c64::g_vic[0x18] & 0xf0) << 6;
    for (uint32_t r = 0; r < 25; r++) {
        char line[41];
        for (uint32_t c = 0; c < 40; c++) {
            uint8_t sc = c64::vicRead(screen + r * 40 + c) & 0x7f;
            line[c] = sc == 0 ? '@' : sc < 27 ? 'A' + sc - 1 : sc < 32 ? '?' : sc < 64 ? sc : '#';
        }
        line[40] = 0;
        ramsyscall_printf("C64 |%s|\n", line);
    }
}

static bool screenHas(const char* s) {
    const uint32_t screen = (c64::g_vic[0x18] & 0xf0) << 6;
    const uint32_t len = __builtin_strlen(s);
    for (uint32_t i = 0; i + len <= 1000; i++) {
        uint32_t k = 0;
        for (; k < len; k++) {
            uint8_t sc = c64::vicRead(screen + i + k) & 0x7f;
            char want = s[k];
            uint8_t code = (want >= 'A' && want <= 'Z') ? want - 'A' + 1 : (uint8_t)want;
            if (sc != code) break;
        }
        if (k == len) return true;
    }
    return false;
}

void C64App::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::NTSC)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void C64App::createScene() { pushScene(&g_scene); }

void C64Scene::start(StartReason) {
    psyqo::GPU& g = gpu();
    for (uint32_t b = 0; b < 256; b++) {
        uint32_t w = 0;
        for (uint32_t k = 0; k < 8; k++) {
            if (b & (0x80 >> k)) w |= 1u << (k * 4);
        }
        s_expand[b] = w;
    }
    for (uint32_t s = 0; s < 2; s++) {
        for (uint32_t c = 0; c < 256; c++) {
            for (uint32_t r = 0; r < 8; r++) {
                s_slotImage[((c >> 5) * 8 + r) * 32 + (c & 31)] = s_expand[c_chargen[s * 2048 + c * 8 + r]];
            }
        }
        psyqo::Rect rect;
        rect.pos = {{.x = (int16_t)c_texX, .y = (int16_t)(s * 64)}};
        rect.size = {{.w = 64, .h = 64}};
        g.uploadToVRAM(reinterpret_cast<const uint16_t*>(s_slotImage), rect);
        scanBlank(s, c_chargen + s * 2048);
    }
    static uint16_t cluts[16 * 16];
    for (uint32_t k = 0; k < 16; k++) {
        for (uint32_t e = 0; e < 16; e++) cluts[k * 16 + e] = 0;
        cluts[k * 16 + 1] = bgr15(c_palette[k]);
    }
    psyqo::Rect rect;
    rect.pos = {{.x = (int16_t)c_texX, .y = (int16_t)c_clutY}};
    rect.size = {{.w = 16, .h = 16}};
    g.uploadToVRAM(cluts, rect);

    COUNTERS[2].mode = 0;  // system clock, free running
    c64::reset({c_basic, c_kernal, c_chargen});
    m6502jit::init(c64::g_cpu);
#ifdef C64_INTERP
    setCore(false);
#else
    setCore(true);
#endif
    ramsyscall_printf("C64: reset, pc=%04x\n", c64::g_cpu.pc);
    s_lastVsync = g.getFrameCount();
}

void C64Scene::frame() {
    psyqo::GPU& g = gpu();
    const uint32_t h0 = hblanks();
    renderBegin();
    c64::runFrame();
    const uint32_t h1 = hblanks();
#ifndef NODRAW
    drawFrame();
#endif
    const uint32_t hb = hblanks();
    renderEnd(g);
    const uint32_t h2 = hblanks();
    s_sumBuild += (uint16_t)(hb - h1);

    const uint32_t emu = (uint16_t)(h1 - h0);
    const uint32_t total = (uint16_t)(h2 - h0);
    s_sumEmu += emu;
    s_sumTotal += total;
    if (total > s_maxTotal) s_maxTotal = total;
    s_frames++;
    const uint32_t vs = g.getFrameCount();
    s_vsyncs += vs - s_lastVsync;
    s_lastVsync = vs;

    if (!s_reportedReady && screenHas("READY.")) {
        s_reportedReady = true;
        ramsyscall_printf("C64: READY. on screen at guest frame %u\n", c64::g_frame);
        dumpScreen();
    }
    if (s_frames == 300) {
        // 15734 hblanks per second. Frame budget at 59.94 Hz is 262.5 hblanks.
        const uint32_t avgEmuUs = s_sumEmu * 6356 / 100 / s_frames;
        const uint32_t avgBuildUs = s_sumBuild * 6356 / 100 / s_frames;
        const uint32_t avgTotUs = s_sumTotal * 6356 / 100 / s_frames;
        const uint32_t maxUs = s_maxTotal * 6356 / 100;
        ramsyscall_printf(
            "C64 TIMING %s frame %u: per guest frame emu %u.%03u ms, build %u.%03u ms, emu+build+send avg %u.%03u ms max %u.%03u ms; "
            "%u guest frames in %u vsyncs; irqs %u bad %u illegal %u sprites-nodes %u rect-nodes %u dropped %u\n",
            s_jit ? "jit" : "interp", c64::g_frame, avgEmuUs / 1000, avgEmuUs % 1000, avgBuildUs / 1000, avgBuildUs % 1000, avgTotUs / 1000, avgTotUs % 1000, maxUs / 1000,
            maxUs % 1000, s_frames, s_vsyncs, c64::stats().irqs, c64::stats().badLines, c64::stats().illegal,
            s_spriteCount, s_rectCount, s_dropped);
#ifdef C64_PROF
        // Counter 2 runs at sysclk (33.8688 MHz): 33869 ticks per ms.
        const uint32_t runUs = c64::g_profRun / s_frames * 1000 / 33869;
        ramsyscall_printf("C64 PROF: in core %u us per frame, %u core calls per frame\n", runUs,
                          c64::g_profCalls / s_frames);
        c64::g_profRun = c64::g_profCalls = 0;
#endif
        if (s_jit) {
            const auto& js = m6502jit::stats();
            ramsyscall_printf(
                "C64 JIT: compiled %u failed %u words %u chain %u budget %u slow %u irq %u smc %u steps %u interpBlocks %u "
                "interpCycles %u rangeFlushes %u flushes %u killed %u\n",
                js.compiled, js.compileFailed, js.codeWords, js.exitsChain, js.exitsBudget, js.exitsSlow, js.exitsIrq, js.exitsSmc,
                js.singleSteps, js.interpBlocks, js.interpCycles, js.rangeFlushes, js.flushes, js.blocksKilled);
        }
        s_sumBuild = s_sumEmu = s_sumTotal = s_maxTotal = s_frames = s_vsyncs = 0;
#ifndef C64_INTERP
        if (s_reportedReady) setCore((++s_window & 1) == 0);
#endif
    }
}

}  // namespace

#ifdef C64_PROF
uint16_t c64::profTick() { return COUNTERS[2].value; }
#endif

int main() { return g_app.run(); }
