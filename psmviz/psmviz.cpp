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

// A music visualiser driven by the PSM player's notifications. The song
// carries USER events (tag 1, payload = section number) marking its sections;
// the player also mirrors the note-ons of the lead, the bells and the drums.
// Each frame drains the queue: notes spawn and pulse shapes, sections change
// the palette and how the shapes move. The shapes otherwise live on their own,
// easing towards targets they pick themselves.

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
constexpr int MAX_SHAPES = 40;
constexpr int MAX_SIDES = 6;
constexpr int MAX_HULL = 24;
constexpr int HULL_FRAMES = 40;
constexpr int FADE_FRAMES = 60;

// MIDI channels the song uses (0-based), and the drum notes the visuals follow.
constexpr unsigned CH_LEAD = 0;
constexpr unsigned CH_BELLS = 1;
constexpr unsigned CH_PAD = 3;
constexpr unsigned CH_DRUMS = 9;
constexpr uint8_t NOTE_KICK = 36;
constexpr uint8_t NOTE_SNARE = 38;
constexpr uint8_t NOTE_CLAP = 39;
constexpr uint8_t NOTE_CRASH = 49;

// USER tag 1 carries the section number.
constexpr uint8_t TAG_SECTION = 1;
enum Section { INTRO, VERSE, BUILD, HOOK, VERSE2, RISER, REVEAL, TAG, END };

struct Palette {
    psyqo::Color bg;
    psyqo::Color c[4];
};

constexpr Palette c_cold = {{{.r = 8, .g = 14, .b = 34}},
                            {{{.r = 70, .g = 110, .b = 200}},
                             {{.r = 40, .g = 170, .b = 210}},
                             {{.r = 120, .g = 90, .b = 210}},
                             {{.r = 180, .g = 200, .b = 230}}}};
constexpr Palette c_hook = {{{.r = 22, .g = 10, .b = 38}},
                            {{{.r = 170, .g = 60, .b = 160}},
                             {{.r = 90, .g = 90, .b = 220}},
                             {{.r = 210, .g = 110, .b = 190}},
                             {{.r = 220, .g = 200, .b = 240}}}};
constexpr Palette c_riser = {{{.r = 2, .g = 2, .b = 8}},
                             {{{.r = 60, .g = 70, .b = 120}},
                              {{.r = 40, .g = 90, .b = 120}},
                              {{.r = 80, .g = 70, .b = 130}},
                              {{.r = 200, .g = 210, .b = 240}}}};
constexpr Palette c_warm = {{{.r = 60, .g = 26, .b = 10}},
                            {{{.r = 255, .g = 190, .b = 60}},
                             {{.r = 250, .g = 120, .b = 60}},
                             {{.r = 255, .g = 230, .b = 150}},
                             {{.r = 255, .g = 255, .b = 240}}}};
constexpr Palette c_black = {};

enum class Motion { WANDER, GATHER, CONVERGE };

struct Shape {
    bool alive;
    bool filled;
    uint8_t sides;
    uint8_t colorIdx;
    uint8_t angle;
    int8_t spin;
    int16_t life;
    int16_t wander;
    // Positions and radii are 24.8 fixed point, in pixels.
    int32_t x, y, tx, ty;
    int32_t r, tr;
};

class PsmViz final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class VizScene final : public psyqo::Scene {
    void start(StartReason reason) override;
    void frame() override;

    void onSection(unsigned section);
    void onNote(unsigned channel, uint8_t note, uint8_t velocity);
    Shape &spawn(int x, int y, int radius, unsigned sides, bool filled, int life);
    void retarget(Shape &s);
    void burst();
    void startHull();
    void update();
    void render();

    Shape m_shapes[MAX_SHAPES];
    Palette m_pal = c_black;
    const Palette *m_target = &c_cold;
    Motion m_motion = Motion::WANDER;
    int m_pulse = 0;
    uint32_t m_lastPadFrame = 0xffffffff;
    int m_hullCount = 0;
    int m_hullFrame = HULL_FRAMES;
    psyqo::Vertex m_hull[MAX_HULL];
    int m_hullCx = 0, m_hullCy = 0;

    static constexpr unsigned OT_SIZE = MAX_SHAPES + 2;
    psyqo::OrderingTable<OT_SIZE> m_ots[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Triangle> m_tris[2][MAX_SHAPES * MAX_SIDES];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Line> m_lines[2][MAX_SHAPES * MAX_SIDES];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Line> m_hullLines[2][MAX_HULL];
};

PsmViz g_app;
VizScene g_scene;
uintptr_t g_musicTimer;

// 256-step sine and cosine, 2.14 fixed point, built with the "magic circle"
// recurrence so no trig library is needed.
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

uint32_t s_seed = 0x2545f491;
uint32_t rnd() {
    s_seed = s_seed * 1103515245 + 12345;
    return s_seed >> 8;
}
int rnd(int lo, int hi) { return lo + int(rnd() % uint32_t(hi - lo + 1)); }

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

uint8_t approach(uint8_t cur, uint8_t target) {
    int d = int(target) - int(cur);
    if (d == 0) return cur;
    int step = d / 12;
    if (step == 0) step = d > 0 ? 1 : -1;
    return uint8_t(cur + step);
}

psyqo::Color approach(psyqo::Color cur, psyqo::Color target) {
    return {{.r = approach(cur.r, target.r), .g = approach(cur.g, target.g), .b = approach(cur.b, target.b)}};
}

// t in 0..256: 0 gives a, 256 gives b.
psyqo::Color mix(psyqo::Color a, psyqo::Color b, int t) {
    auto m = [t](uint8_t x, uint8_t y) { return uint8_t(x + (int(y) - int(x)) * t / 256); };
    return {{.r = m(a.r, b.r), .g = m(a.g, b.g), .b = m(a.b, b.b)}};
}

psyqo::Color brighten(psyqo::Color c, int amount) {
    return {{.r = uint8_t(clampi(c.r + amount, 0, 255)),
             .g = uint8_t(clampi(c.g + amount, 0, 255)),
             .b = uint8_t(clampi(c.b + amount, 0, 255))}};
}

psyqo::Vertex vertex(int x, int y) {
    psyqo::Vertex v;
    v.x = int16_t(x);
    v.y = int16_t(y);
    return v;
}

}  // namespace

void PsmViz::prepare() {
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

void PsmViz::createScene() {
    pushScene(&g_scene);
    // The player runs on its own tick; PSM_hblanks can change with the tempo,
    // so the period is re-read after every poll.
    g_musicTimer = gpu().armPeriodicTimer(PSM_hblanks * psyqo::GPU::US_PER_HBLANK, [this](uint32_t) {
        PSM_Poll();
        gpu().changeTimerPeriod(g_musicTimer, PSM_hblanks * psyqo::GPU::US_PER_HBLANK);
    });
}

void VizScene::start(StartReason reason) {
    for (auto &s : m_shapes) s.alive = false;
}

Shape &VizScene::spawn(int x, int y, int radius, unsigned sides, bool filled, int life) {
    // A free slot, or else the shape closest to the end of its life.
    Shape *slot = &m_shapes[0];
    for (auto &s : m_shapes) {
        if (!s.alive) {
            slot = &s;
            break;
        }
        if (s.life < slot->life) slot = &s;
    }
    Shape &s = *slot;
    s.alive = true;
    s.filled = filled;
    s.sides = uint8_t(sides);
    s.colorIdx = uint8_t(rnd() % 4);
    s.angle = uint8_t(rnd());
    s.spin = int8_t(rnd(-3, 3));
    if (s.spin == 0) s.spin = 1;
    s.life = int16_t(life);
    s.x = s.tx = x << 8;
    s.y = s.ty = y << 8;
    s.r = 0;
    s.tr = radius << 8;
    retarget(s);
    return s;
}

void VizScene::retarget(Shape &s) {
    int cx = s.x >> 8, cy = s.y >> 8;
    switch (m_motion) {
        case Motion::WANDER:
            cx = clampi(cx + rnd(-90, 90), 16, WIDTH - 16);
            cy = clampi(cy + rnd(-70, 70), 16, HEIGHT - 16);
            break;
        case Motion::GATHER:
            cx = WIDTH / 2 + rnd(-50, 50);
            cy = HEIGHT / 2 + rnd(-40, 40);
            break;
        case Motion::CONVERGE:
            cx = WIDTH / 2 + rnd(-6, 6);
            cy = HEIGHT / 2 + rnd(-6, 6);
            break;
    }
    s.tx = cx << 8;
    s.ty = cy << 8;
    s.wander = int16_t(rnd(60, 180));
}

// Push every shape away from the centre of the screen.
void VizScene::burst() {
    for (auto &s : m_shapes) {
        if (!s.alive) continue;
        int dx = (s.x >> 8) - WIDTH / 2, dy = (s.y >> 8) - HEIGHT / 2;
        if (dx == 0 && dy == 0) dx = rnd(-8, 8), dy = rnd(-8, 8);
        s.tx = clampi(WIDTH / 2 + dx * 3 + rnd(-30, 30), 8, WIDTH - 8) << 8;
        s.ty = clampi(HEIGHT / 2 + dy * 3 + rnd(-30, 30), 8, HEIGHT - 8) << 8;
        s.wander = int16_t(rnd(90, 150));
    }
}

void VizScene::onSection(unsigned section) {
    ramsyscall_printf("psmviz: section %u at frame %u, %u notifications dropped\n", section,
                      g_app.gpu().getFrameCount(), PSM_notifyDropped);
    switch (section) {
        case INTRO:
        case VERSE:
            m_target = &c_cold;
            m_motion = Motion::WANDER;
            break;
        case BUILD:
            m_target = &c_cold;
            m_motion = Motion::GATHER;
            for (auto &s : m_shapes) {
                if (s.alive) retarget(s);
            }
            break;
        case HOOK:
            m_target = &c_hook;
            m_motion = Motion::WANDER;
            burst();
            break;
        case VERSE2:
            m_target = &c_cold;
            m_motion = Motion::WANDER;
            break;
        case RISER:
            m_target = &c_riser;
            m_motion = Motion::CONVERGE;
            for (auto &s : m_shapes) {
                if (!s.alive) continue;
                retarget(s);
                s.tr = 4 << 8;
            }
            break;
        case REVEAL:
            // A white flash that settles into the warm palette, everything
            // thrown outwards, and one big slow hexagon behind it all.
            m_pal.bg = {{.r = 255, .g = 255, .b = 255}};
            m_target = &c_warm;
            m_motion = Motion::WANDER;
            for (auto &s : m_shapes) {
                if (s.alive) s.tr = rnd(10, 26) << 8;
            }
            burst();
            {
                Shape &big = spawn(WIDTH / 2, HEIGHT / 2, 100, 6, false, 30 * 60);
                big.spin = 1;
                big.colorIdx = 3;
                big.tx = big.x;
                big.ty = big.y;
                big.wander = 0x7fff;
            }
            startHull();
            break;
        case TAG:
            m_target = &c_warm;
            m_motion = Motion::WANDER;
            break;
        case END:
            m_target = &c_black;
            for (auto &s : m_shapes) {
                if (s.alive && s.life > 90) s.life = 90;
            }
            break;
    }
}

void VizScene::onNote(unsigned channel, uint8_t note, uint8_t velocity) {
    if (channel == CH_DRUMS) {
        if (note == NOTE_KICK) {
            m_pulse = 20;
            for (auto &s : m_shapes) {
                if (s.alive) s.r += s.r >> 2;
            }
        } else if (note == NOTE_SNARE || note == NOTE_CLAP) {
            Shape &s = spawn(rnd(16, WIDTH - 16), rnd(16, HEIGHT - 16), 8, 3, true, 40);
            s.spin = int8_t(rnd(0, 1) ? 8 : -8);
        } else if (note == NOTE_CRASH) {
            startHull();
        }
    } else if (channel == CH_LEAD) {
        // Pitch sets the horizontal position: low notes left, high notes right.
        int x = clampi(16 + (int(note) - 62) * (WIDTH - 32) / 36, 16, WIDTH - 16);
        spawn(x, rnd(40, HEIGHT - 40), 8 + velocity / 8, 3 + note % 4, true, rnd(180, 300));
    } else if (channel == CH_PAD) {
        // One chord is several note-ons in the same frame: draw one shape per chord.
        uint32_t now = g_app.gpu().getFrameCount();
        if (now == m_lastPadFrame) return;
        m_lastPadFrame = now;
        Shape &s = spawn(rnd(60, WIDTH - 60), rnd(50, HEIGHT - 50), rnd(50, 80), 4, false, 110);
        s.spin = int8_t(rnd(0, 1) ? 1 : -1);
        s.colorIdx = 0;
    } else if (channel == CH_BELLS) {
        Shape &s = spawn(rnd(40, WIDTH - 40), rnd(40, HEIGHT - 40), rnd(30, 50), 6, false, 150);
        s.spin = 1;
        s.colorIdx = 3;
    }
}

// The convex hull of the live shapes' centres, which then expands and fades.
void VizScene::startHull() {
    psyqo::Vertex pts[MAX_SHAPES];
    int n = 0;
    for (auto &s : m_shapes) {
        if (s.alive) pts[n++] = vertex(s.x >> 8, s.y >> 8);
    }
    if (n < 3) return;
    // Sort by x, then y.
    for (int i = 1; i < n; i++) {
        psyqo::Vertex v = pts[i];
        int j = i - 1;
        while (j >= 0 && (pts[j].x > v.x || (pts[j].x == v.x && pts[j].y > v.y))) {
            pts[j + 1] = pts[j];
            j--;
        }
        pts[j + 1] = v;
    }
    auto cross = [](psyqo::Vertex o, psyqo::Vertex a, psyqo::Vertex b) {
        return (int32_t(a.x) - o.x) * (int32_t(b.y) - o.y) - (int32_t(a.y) - o.y) * (int32_t(b.x) - o.x);
    };
    // Andrew's monotone chain: lower hull, then upper hull.
    psyqo::Vertex hull[2 * MAX_SHAPES];
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) k--;
        hull[k++] = pts[i];
    }
    for (int i = n - 2, lower = k + 1; i >= 0; i--) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) k--;
        hull[k++] = pts[i];
    }
    k--;  // the last point repeats the first
    if (k < 3) return;
    if (k > MAX_HULL) k = MAX_HULL;
    int cx = 0, cy = 0;
    for (int i = 0; i < k; i++) {
        m_hull[i] = hull[i];
        cx += hull[i].x;
        cy += hull[i].y;
    }
    m_hullCount = k;
    m_hullCx = cx / k;
    m_hullCy = cy / k;
    m_hullFrame = 0;
}

void VizScene::update() {
    for (unsigned i = 0; i < 4; i++) m_pal.c[i] = approach(m_pal.c[i], m_target->c[i]);
    m_pal.bg = approach(m_pal.bg, m_target->bg);
    if (m_pulse > 0) m_pulse -= m_pulse / 4 + 1;

    for (auto &s : m_shapes) {
        if (!s.alive) continue;
        s.x += (s.tx - s.x) >> 4;
        s.y += (s.ty - s.y) >> 4;
        s.r += (s.tr - s.r) >> 3;
        s.angle += s.spin;
        if (--s.wander <= 0) retarget(s);
        if (--s.life <= 0) s.alive = false;
    }
}

void VizScene::render() {
    auto &gpu = g_app.gpu();
    int parity = gpu.getParity();
    auto &ot = m_ots[parity];
    auto &clear = m_clear[parity];

    gpu.getNextClear(clear.primitive, brighten(m_pal.bg, m_pulse));
    gpu.chain(clear);

    // Older slots draw first; the ordering table draws higher z first.
    unsigned tri = 0, line = 0;
    for (unsigned i = 0; i < MAX_SHAPES; i++) {
        auto &s = m_shapes[i];
        if (!s.alive) continue;
        psyqo::Color c = m_pal.c[s.colorIdx];
        psyqo::Color edge = brighten(c, 40);
        if (s.life < FADE_FRAMES) {
            int t = (FADE_FRAMES - s.life) * 256 / FADE_FRAMES;
            c = mix(c, m_pal.bg, t);
            edge = mix(edge, m_pal.bg, t);
        }
        int cx = s.x >> 8, cy = s.y >> 8, r = s.r >> 8;
        psyqo::Vertex v[MAX_SIDES];
        for (unsigned k = 0; k < s.sides; k++) {
            uint8_t a = uint8_t(s.angle + k * 256 / s.sides);
            v[k] = vertex(cx + ((r * s_cos[a]) >> 14), cy + ((r * s_sin[a]) >> 14));
        }
        unsigned z = OT_SIZE - 2 - i;
        for (unsigned k = 0; k < s.sides; k++) {
            psyqo::Vertex a = v[k], b = v[(k + 1) % s.sides];
            if (s.filled) {
                auto &t = m_tris[parity][tri++];
                t.primitive.setColor(c);
                t.primitive.setSemiTrans();
                t.primitive.pointA = vertex(cx, cy);
                t.primitive.pointB = a;
                t.primitive.pointC = b;
                ot.insert(t, z);
            }
            auto &l = m_lines[parity][line++];
            l.primitive.setColor(edge);
            l.primitive.pointA = a;
            l.primitive.pointB = b;
            ot.insert(l, z);
        }
    }

    if (m_hullFrame < HULL_FRAMES) {
        int f = m_hullFrame++;
        psyqo::Color c = mix(m_pal.c[3], m_pal.bg, f * 256 / HULL_FRAMES);
        int scale = 256 + f * 192 / HULL_FRAMES;  // 1.0 to 1.75
        psyqo::Vertex v[MAX_HULL];
        for (int i = 0; i < m_hullCount; i++) {
            v[i] = vertex(m_hullCx + ((m_hull[i].x - m_hullCx) * scale >> 8),
                          m_hullCy + ((m_hull[i].y - m_hullCy) * scale >> 8));
        }
        for (int i = 0; i < m_hullCount; i++) {
            auto &l = m_hullLines[parity][i];
            l.primitive.setColor(c);
            l.primitive.pointA = v[i];
            l.primitive.pointB = v[(i + 1) % m_hullCount];
            ot.insert(l, OT_SIZE - 1);
        }
    }

    gpu.chain(ot);
}

void VizScene::frame() {
    PSM_Notify n;
    while (PSM_PopNotify(&n)) {
        if (n.kind == PSM_NOTIFY_USER) {
            if (n.tag == TAG_SECTION) onSection(n.data);
        } else {
            onNote(n.tag, n.data & 0xff, (n.data >> 8) & 0xff);
        }
    }
    update();
    render();
}

int main() { return g_app.run(); }
