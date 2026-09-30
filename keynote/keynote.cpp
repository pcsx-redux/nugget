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

// A choreographed visual for the keynote song, driven by the PSM player's
// notifications. The song carries USER events (tag 1, payload = section
// number) at each section, and the player mirrors the note-ons of the lead,
// the bells, the pad and the drums.
//
// Each section has a fixed formation: where the 24 dots sit, how big the
// centre hexagon is and how far its six petals are unfolded. Formations are
// functions of the beat within the section, so the motion follows the song's
// structure; the dots ease towards them every frame. Notes only light up and
// pulse parts of the formation that is already there:
//   intro    one hexagon, growing a step per chord, a ripple per chord
//   verse    a ring of 12 dots stepping round once per bar; each lead note
//            lights the dot at its pitch class
//   build    two counter-rotating rings closing in, faster each bar
//   hook     a 6x4 grid; kicks run down it as a wave, notes light a column
//   verse 2  two rings again, the bells rippling out from the centre
//   riser    everything collapses into one point
//   reveal   white flash, the hexagon unfolds into six petals, rings return
//   tag, end the petals fold back and everything shrinks away

#include <stdint.h>

#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/lines.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/triangles.hh"
#include "psyqo/scene.hh"

extern "C" {
#include "psmplayer/psmplayer.h"
extern const uint8_t _binary_keynote_psm_start[];
extern const uint8_t _binary_keynote_psm_end[];
extern const uint8_t _binary_keynote_vab_start[];
extern const uint8_t _binary_keynote_vab_end[];
}

namespace {

constexpr int WIDTH = 320;
constexpr int HEIGHT = 240;
constexpr int CX = WIDTH / 2;
constexpr int CY = HEIGHT / 2;
constexpr unsigned BPM = 125;

constexpr unsigned NUM_DOTS = 24;
constexpr unsigned RING = 12;
constexpr unsigned MAX_DOT_SIDES = 6;
constexpr unsigned PETALS = 6;
constexpr unsigned MAX_RIPPLES = 8;
constexpr unsigned RIPPLE_SIDES = 24;

// MIDI channels of the song (0-based), and the drum notes followed.
constexpr unsigned CH_LEAD = 0;
constexpr unsigned CH_BELLS = 1;
constexpr unsigned CH_PAD = 3;
constexpr unsigned CH_DRUMS = 9;
constexpr uint8_t NOTE_KICK = 36;
constexpr uint8_t NOTE_CRASH = 49;

constexpr uint8_t TAG_SECTION = 1;
enum Section { INTRO, VERSE, BUILD, HOOK, VERSE2, RISER, REVEAL, TAG, END, NONE };

struct Palette {
    psyqo::Color bg;
    psyqo::Color c[4];  // dots from c[0] to c[1], hexagon from c[2] to c[3]
};

constexpr psyqo::Color rgb(uint8_t r, uint8_t g, uint8_t b) { return {{.r = r, .g = g, .b = b}}; }

constexpr Palette c_cold = {rgb(6, 10, 26), {rgb(60, 110, 210), rgb(90, 200, 230), rgb(150, 170, 240), rgb(230, 240, 255)}};
constexpr Palette c_hook = {rgb(18, 8, 32), {rgb(120, 70, 220), rgb(220, 90, 180), rgb(200, 150, 240), rgb(255, 230, 250)}};
constexpr Palette c_dark = {rgb(0, 0, 0), {rgb(80, 90, 140), rgb(80, 90, 140), rgb(230, 240, 255), rgb(255, 255, 255)}};
constexpr Palette c_warm = {rgb(40, 16, 6), {rgb(255, 120, 50), rgb(255, 200, 70), rgb(255, 170, 60), rgb(255, 245, 210)}};
constexpr Palette c_black = {};

// Where one dot should be, as a formation places it.
struct Place {
    int x, y, r;  // pixels
    uint8_t sides;
};

struct Dot {
    int32_t x, y, r;  // 24.8 fixed point, eased towards the formation
    uint8_t sides;
    uint8_t glow;
};

struct Ripple {
    bool alive;
    uint8_t colorIdx;
    int16_t frame, duration;
    int16_t r0, r1;
};

class Keynote final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class KeynoteScene final : public psyqo::Scene {
    void frame() override;

    void onSection(unsigned section);
    void onNote(unsigned channel, uint8_t note);
    void ripple(int r0, int r1, int duration, uint8_t colorIdx);
    Place formation(unsigned i, int beat256);
    void heroTarget(int beat256, int &radius, int &unfold);
    void update();
    void render();
    int kickPulse(int delay) const;

    unsigned m_section = NONE;
    uint32_t m_sectionFrame = 0;
    uint32_t m_kickFrame = 0x80000000;
    unsigned m_chords = 0;
    Palette m_pal = c_black;
    const Palette *m_target = &c_cold;

    Dot m_dots[NUM_DOTS] = {};
    int32_t m_heroR = 0, m_heroUnfold = 0;  // 24.8
    uint8_t m_heroAngle = 0;
    uint8_t m_heroGlow = 0;
    int m_spin = 0;  // ring rotation, 16.16 of a turn
    Ripple m_ripples[MAX_RIPPLES] = {};

    static constexpr unsigned OT_SIZE = 4;
    psyqo::OrderingTable<OT_SIZE> m_ots[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Triangle> m_dotTris[2][NUM_DOTS * MAX_DOT_SIDES];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Triangle> m_petals[2][PETALS];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Line> m_rippleLines[2][MAX_RIPPLES * RIPPLE_SIDES];
};

Keynote g_app;
KeynoteScene g_scene;
uintptr_t g_musicTimer;

// 256-step sine and cosine, 2.14 fixed point, from the "magic circle" recurrence.
int16_t s_sin[256], s_cos[256];

void buildSine() {
    int32_t x = 1 << 14, y = 0;
    constexpr int32_t k = 1608;  // 2*pi/256 in 16.16
    for (unsigned i = 0; i < 256; i++) {
        s_sin[i] = int16_t(y);
        s_cos[i] = int16_t(x);
        x -= (y * k) >> 16;
        y += (x * k) >> 16;
    }
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Smoothstep on 0..256.
int ease(int t) {
    if (t <= 0) return 0;
    if (t >= 256) return 256;
    return (t * t * (768 - 2 * t)) >> 16;
}

int lerpi(int a, int b, int t) { return a + (b - a) * t / 256; }

uint8_t approach(uint8_t cur, uint8_t target) {
    int d = int(target) - int(cur);
    if (d == 0) return cur;
    int step = d / 10;
    if (step == 0) step = d > 0 ? 1 : -1;
    return uint8_t(cur + step);
}

psyqo::Color approach(psyqo::Color cur, psyqo::Color target) {
    return rgb(approach(cur.r, target.r), approach(cur.g, target.g), approach(cur.b, target.b));
}

// t in 0..256: 0 gives a, 256 gives b.
psyqo::Color mix(psyqo::Color a, psyqo::Color b, int t) {
    auto m = [t](uint8_t x, uint8_t y) { return uint8_t(x + (int(y) - int(x)) * t / 256); };
    return rgb(m(a.r, b.r), m(a.g, b.g), m(a.b, b.b));
}

psyqo::Vertex vertex(int x, int y) {
    psyqo::Vertex v;
    v.x = int16_t(x);
    v.y = int16_t(y);
    return v;
}

// A point at distance r (pixels) and angle a (256 = full turn) from (x, y).
psyqo::Vertex polar(int x, int y, int r, uint8_t a) {
    return vertex(x + ((r * s_cos[a]) >> 14), y + ((r * s_sin[a]) >> 14));
}

}  // namespace

void Keynote::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
    buildSine();

    PSM_LoadBank(_binary_keynote_vab_start, _binary_keynote_vab_end - _binary_keynote_vab_start);
    PSM_noteMirrorMask = (1 << CH_LEAD) | (1 << CH_BELLS) | (1 << CH_PAD) | (1 << CH_DRUMS);
    PSM_LoadSong(_binary_keynote_psm_start, _binary_keynote_psm_end - _binary_keynote_psm_start);
}

void Keynote::createScene() {
    pushScene(&g_scene);
    g_musicTimer = gpu().armPeriodicTimer(PSM_hblanks * psyqo::GPU::US_PER_HBLANK, [this](uint32_t) {
        PSM_Poll();
        gpu().changeTimerPeriod(g_musicTimer, PSM_hblanks * psyqo::GPU::US_PER_HBLANK);
    });
}

void KeynoteScene::ripple(int r0, int r1, int duration, uint8_t colorIdx) {
    Ripple *slot = &m_ripples[0];
    for (auto &r : m_ripples) {
        if (!r.alive) {
            slot = &r;
            break;
        }
        if (r.frame > slot->frame) slot = &r;
    }
    *slot = {true, colorIdx, 0, int16_t(duration), int16_t(r0), int16_t(r1)};
}

void KeynoteScene::onSection(unsigned section) {
    m_section = section;
    m_sectionFrame = g_app.gpu().getFrameCount();
    switch (section) {
        case INTRO:
            m_target = &c_cold;
            m_chords = 0;
            break;
        case VERSE:
        case BUILD:
        case VERSE2:
            m_target = &c_cold;
            break;
        case HOOK:
            m_target = &c_hook;
            ripple(20, 200, 50, 3);
            break;
        case RISER:
            m_target = &c_dark;
            break;
        case REVEAL:
            m_pal.bg = rgb(255, 255, 255);
            m_target = &c_warm;
            m_heroGlow = 255;
            ripple(0, 220, 70, 3);
            break;
        case TAG:
            m_target = &c_warm;
            break;
        case END:
            m_target = &c_black;
            break;
    }
}

void KeynoteScene::onNote(unsigned channel, uint8_t note) {
    uint32_t now = g_app.gpu().getFrameCount();
    if (channel == CH_DRUMS) {
        if (note == NOTE_KICK) {
            m_kickFrame = now;
        } else if (note == NOTE_CRASH) {
            ripple(30, 200, 45, 3);
        }
    } else if (channel == CH_LEAD) {
        // The melody lights the ring at its pitch class, or in the hook's grid,
        // the column at its pitch class.
        if (m_section == HOOK) {
            unsigned col = note % 6;
            for (unsigned row = 0; row < 4; row++) m_dots[row * 6 + col].glow = 255;
        } else {
            m_dots[note % RING].glow = 255;
            if (m_section == BUILD || m_section == VERSE2 || m_section == REVEAL) m_dots[RING + note % RING].glow = 160;
        }
        if (m_section == REVEAL || m_section == TAG) m_heroGlow = 120;
    } else if (channel == CH_BELLS) {
        ripple(24, 150, 60, 1);
    } else if (channel == CH_PAD && m_section == INTRO) {
        // A chord is several note-ons at once; count each chord once.
        static uint32_t lastFrame = 0xffffffff;
        if (now == lastFrame) return;
        lastFrame = now;
        m_chords++;
        ripple(8 + m_chords * 6, 70 + m_chords * 10, 80, 2);
    }
}

// How strongly a kick `delay` frames ago still shows, 0..256.
int KeynoteScene::kickPulse(int delay) const {
    int age = int(g_app.gpu().getFrameCount() - m_kickFrame) - delay;
    if (age < 0 || age >= 12) return 0;
    return 256 - age * 256 / 12;
}

Place KeynoteScene::formation(unsigned i, int beat256) {
    int bar256 = beat256 / 4;  // bars since the section started, 8.8
    int bar = bar256 >> 8;
    bool inner = i >= RING;
    unsigned k = i % RING;
    Place p = {CX, CY, 0, 6};

    // Rings step round one twelfth of a turn per bar, easing into each step.
    auto ringAngle = [&](int stepsPerBar, bool reverse) {
        int steps = bar * stepsPerBar * 256 + ease(bar256 & 255) * stepsPerBar;
        int a = k * 256 / RING + (steps * 256 / RING >> 8) + (m_spin >> 8);
        if (inner) a += 256 / (2 * RING);
        return uint8_t(reverse ? -a : a);
    };

    switch (m_section) {
        case VERSE: {
            if (inner) break;
            psyqo::Vertex v = polar(CX, CY, 84, ringAngle(1, false));
            p = {v.x, v.y, 7, 6};
            break;
        }
        case BUILD:
        case VERSE2: {
            int t = m_section == BUILD ? clampi(bar256 * 256 / (8 * 256), 0, 256) : 0;
            int radius = inner ? lerpi(46, 30, t) : lerpi(84, 58, t);
            psyqo::Vertex v = polar(CX, CY, radius, ringAngle(m_section == BUILD ? 1 + bar / 2 : 1, inner));
            p = {v.x, v.y, inner ? 5 : 7, 6};
            break;
        }
        case HOOK: {
            unsigned col = i % 6, row = i / 6;
            p = {CX - 5 * 44 / 2 + int(col) * 44, CY - 3 * 44 / 2 + int(row) * 44, 12, 4};
            break;
        }
        case RISER:
            p = {CX, CY, 2, 6};
            break;
        case REVEAL:
        case TAG: {
            psyqo::Vertex v = polar(CX, CY, inner ? 104 : 90, ringAngle(1, inner));
            p = {v.x, v.y, inner ? 4 : 7, 6};
            break;
        }
        default:
            break;
    }
    return p;
}

void KeynoteScene::heroTarget(int beat256, int &radius, int &unfold) {
    int bar256 = beat256 / 4;
    switch (m_section) {
        case INTRO:
            radius = 6 + int(m_chords) * 7;
            unfold = 0;
            break;
        case VERSE:
        case VERSE2:
            radius = 26;
            unfold = 0;
            break;
        case BUILD:
            radius = lerpi(26, 38, clampi(bar256 / 8, 0, 256));
            unfold = 0;
            break;
        case HOOK:
            radius = 0;
            unfold = 0;
            break;
        case RISER:
            radius = lerpi(20, 2, clampi(bar256 / 2, 0, 256));
            unfold = 0;
            break;
        case REVEAL:
            radius = 46;
            unfold = lerpi(0, 22, ease(bar256));
            break;
        case TAG:
            radius = 46;
            unfold = lerpi(22, 0, ease(bar256 / 2));
            break;
        default:
            radius = 0;
            unfold = 0;
            break;
    }
}

void KeynoteScene::update() {
    for (unsigned i = 0; i < 4; i++) m_pal.c[i] = approach(m_pal.c[i], m_target->c[i]);
    m_pal.bg = approach(m_pal.bg, m_target->bg);

    auto &gpu = g_app.gpu();
    // Beats since the section's cue, 8.8 fixed point.
    int frames = int(gpu.getFrameCount() - m_sectionFrame);
    int beat256 = frames * 256 * int(BPM) / (60 * int(gpu.getRefreshRate()));

    // The build and the riser add a steadily faster spin on top of the bar steps.
    if (m_section == BUILD) m_spin += 16 + beat256 / 16;
    if (m_section == RISER) m_spin += 256;

    for (unsigned i = 0; i < NUM_DOTS; i++) {
        auto &d = m_dots[i];
        Place p = formation(i, beat256);
        // The hook's kick wave runs down the grid a row every 3 frames.
        int pulse = kickPulse(m_section == HOOK ? int(i / 6) * 3 : 0);
        int r = p.r * (256 + pulse / 3) / 256;
        d.x += ((p.x << 8) - d.x) >> 3;
        d.y += ((p.y << 8) - d.y) >> 3;
        d.r += ((r << 8) - d.r) >> 2;
        d.sides = p.sides;
        d.glow = d.glow > 10 ? d.glow - 10 : 0;
    }

    int radius, unfold;
    heroTarget(beat256, radius, unfold);
    if (m_section == REVEAL || m_section == TAG) unfold += kickPulse(0) * 6 / 256;
    m_heroR += ((radius << 8) - m_heroR) >> 3;
    m_heroUnfold += ((unfold << 8) - m_heroUnfold) >> 2;
    m_heroAngle += (m_section == BUILD || m_section == RISER) ? 2 : 1;
    m_heroGlow = m_heroGlow > 6 ? m_heroGlow - 6 : 0;

    for (auto &r : m_ripples) {
        if (r.alive && ++r.frame >= r.duration) r.alive = false;
    }
}

void KeynoteScene::render() {
    auto &gpu = g_app.gpu();
    int parity = gpu.getParity();
    auto &ot = m_ots[parity];

    gpu.getNextClear(m_clear[parity].primitive, m_pal.bg);
    gpu.chain(m_clear[parity]);

    // Ripples at the back (highest z draws first), then dots, then the hexagon.
    unsigned line = 0;
    for (auto &r : m_ripples) {
        if (!r.alive) continue;
        int t = r.frame * 256 / r.duration;
        int radius = lerpi(r.r0, r.r1, ease(t));
        psyqo::Color c = mix(m_pal.c[r.colorIdx], m_pal.bg, t);
        for (unsigned k = 0; k < RIPPLE_SIDES; k++) {
            auto &l = m_rippleLines[parity][line++];
            l.primitive.setColor(c);
            l.primitive.pointA = polar(CX, CY, radius, uint8_t(k * 256 / RIPPLE_SIDES));
            l.primitive.pointB = polar(CX, CY, radius, uint8_t((k + 1) * 256 / RIPPLE_SIDES));
            ot.insert(l, 3);
        }
    }

    unsigned tri = 0;
    for (unsigned i = 0; i < NUM_DOTS; i++) {
        auto &d = m_dots[i];
        int r = d.r >> 8;
        if (r <= 0) continue;
        int x = d.x >> 8, y = d.y >> 8;
        // Dots shade along the ring from c[0] to c[1]; a lit dot flares to c[3].
        psyqo::Color c = mix(m_pal.c[0], m_pal.c[1], int(i % RING) * 256 / RING);
        c = mix(c, m_pal.c[3], d.glow);
        uint8_t base = d.sides == 4 ? 32 : 0;
        for (unsigned k = 0; k < d.sides; k++) {
            auto &t = m_dotTris[parity][tri++];
            t.primitive.setColor(c);
            t.primitive.setOpaque();
            t.primitive.pointA = vertex(x, y);
            t.primitive.pointB = polar(x, y, r, uint8_t(base + k * 256 / d.sides));
            t.primitive.pointC = polar(x, y, r, uint8_t(base + (k + 1) * 256 / d.sides));
            ot.insert(t, 2);
        }
    }

    int hr = m_heroR >> 8, unfold = m_heroUnfold >> 8;
    if (hr > 0) {
        for (unsigned k = 0; k < PETALS; k++) {
            uint8_t a0 = uint8_t(m_heroAngle + k * 256 / PETALS);
            uint8_t a1 = uint8_t(a0 + 256 / PETALS);
            psyqo::Vertex o = polar(CX, CY, unfold, uint8_t(a0 + 256 / (2 * PETALS)));
            psyqo::Color c = mix(m_pal.c[2], m_pal.c[3], int(k) * 256 / PETALS);
            c = mix(c, rgb(255, 255, 255), m_heroGlow);
            auto &t = m_petals[parity][k];
            t.primitive.setColor(c);
            t.primitive.setOpaque();
            t.primitive.pointA = o;
            t.primitive.pointB = polar(o.x, o.y, hr, a0);
            t.primitive.pointC = polar(o.x, o.y, hr, a1);
            ot.insert(t, 1);
        }
    }

    gpu.chain(ot);
}

void KeynoteScene::frame() {
    PSM_Notify n;
    while (PSM_PopNotify(&n)) {
        if (n.kind == PSM_NOTIFY_USER) {
            if (n.tag == TAG_SECTION) onSection(n.data);
        } else {
            onNote(n.tag, n.data & 0xff);
        }
    }
    update();
    render();
}

int main() { return g_app.run(); }
