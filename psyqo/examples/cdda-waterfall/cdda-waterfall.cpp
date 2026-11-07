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

#include <EASTL/array.h>
#include <stdint.h>

#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/cdrom-device.hh"
#include "psyqo/font.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/common.hh"
#include "psyqo/primitives/control.hh"
#include "psyqo/primitives/quads.hh"
#include "psyqo/scene.hh"
#include "psyqo/soft-math.hh"
#include "psyqo/trigonometry.hh"
#include "spectrum.hh"
#include "spu-capture.hh"

using namespace psyqo::trig_literals;
using namespace psyqo::timer_literals;

namespace {

// Plays CD audio and draws it as a spectrum waterfall: every frame, the latest
// capture window becomes one row of BANDS heights at the front of the field,
// and the older rows scroll back into the distance.
//
// The pieces: SpuCapture pulls the CD audio the SPU writes into its capture
// buffers, Spectrum::computeBands turns a window into band magnitudes, and the
// scene projects the height field with the GTE into one ordering table.

constexpr unsigned COLUMNS = Spectrum::BANDS;
constexpr unsigned ROWS = 24;
constexpr unsigned VERTICES = COLUMNS * ROWS;
constexpr unsigned QUADS = (COLUMNS - 1) * (ROWS - 1);
static_assert(VERTICES % 3 == 0, "the field is projected three vertices at a time");

// World units: the field is 992 wide, 920 deep, and the camera looks at it
// from above and in front.
constexpr int16_t COLUMN_STEP = 32;
constexpr int16_t ROW_STEP = 40;
constexpr int16_t MAX_HEIGHT = 200;

class Waterfall final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
    void loopPlaybackTrack(unsigned track);

  public:
    psyqo::Font<> m_font;
    psyqo::Trig<> m_trig;
    psyqo::CDRomDevice m_cdrom;
    psyqo::CDRomDevice::PlaybackLocation m_playbackLocation;
    SpuCapture m_capture;
    bool m_cddaPlaying = false;
    bool m_getlocPending = false;
};

class WaterfallScene final : public psyqo::Scene {
    void start(StartReason reason) override;
    void frame() override;

    void pushRow();
    void project();
    static psyqo::Color heightColor(unsigned height, unsigned age);

    // Heights as a ring of rows: m_head is the newest row, which sits at the
    // front of the field.
    uint8_t m_heights[ROWS][COLUMNS] = {};
    unsigned m_head = 0;

    // Screen coordinates of every vertex, row by row from the front.
    psyqo::Vertex m_screen[VERTICES];
    psyqo::Color m_colors[VERTICES];

    psyqo::OrderingTable<ROWS> m_ots[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    eastl::array<psyqo::Fragments::SimpleFragment<psyqo::Prim::GouraudQuad>, QUADS> m_quads[2];
};

Waterfall waterfall;
WaterfallScene waterfallScene;

}  // namespace

void Waterfall::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
    m_cdrom.prepare();
}

void Waterfall::loopPlaybackTrack(unsigned track) {
    // Called once when the track starts and once when it ends.
    m_cdrom.playCDDATrack(track, [this, track](bool success) {
        if (!success) {
            syscall_puts("CDDA playback failed\n");
            m_cddaPlaying = false;
            return;
        }
        m_cddaPlaying = !m_cddaPlaying;
        if (!m_cddaPlaying) loopPlaybackTrack(track);
    });
}

void Waterfall::createScene() {
    m_font.uploadSystemFont(gpu());
    pushScene(&waterfallScene);
    m_cdrom.resetBlocking(gpu());
    m_capture.prepare();
    loopPlaybackTrack(2);
    // A capture half lasts 5.8 ms, so poll well inside that.
    gpu().armPeriodicTimer(1_ms, [this](uint32_t) { m_capture.poll(); });
    gpu().armPeriodicTimer(100_ms, [this](uint32_t) {
        if (!m_getlocPending && m_cddaPlaying) {
            m_getlocPending = true;
            m_cdrom.getPlaybackLocation(&m_playbackLocation, [this](auto) { m_getlocPending = false; });
        }
    });
}

void WaterfallScene::start(StartReason reason) {
    psyqo::GTE::clear<psyqo::GTE::Register::TRX, psyqo::GTE::Unsafe>();
    psyqo::GTE::write<psyqo::GTE::Register::TRY, psyqo::GTE::Unsafe>(260);
    psyqo::GTE::write<psyqo::GTE::Register::TRZ, psyqo::GTE::Unsafe>(760);
    psyqo::GTE::write<psyqo::GTE::Register::OFX, psyqo::GTE::Unsafe>(psyqo::FixedPoint<16>(160.0).raw());
    psyqo::GTE::write<psyqo::GTE::Register::OFY, psyqo::GTE::Unsafe>(psyqo::FixedPoint<16>(120.0).raw());
    psyqo::GTE::write<psyqo::GTE::Register::H, psyqo::GTE::Unsafe>(180);
    // Tilt the field towards the camera so the rows behind rise up the screen.
    auto tilt = psyqo::SoftMath::generateRotationMatrix33(-0.25_pi, psyqo::SoftMath::Axis::X, waterfall.m_trig);
    psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::Rotation>(tilt);
}

void WaterfallScene::pushRow() {
    uint16_t bands[Spectrum::BANDS];
    Spectrum::computeBands(waterfall.m_capture.window(), bands);
    m_head = (m_head + ROWS - 1) % ROWS;
    for (unsigned c = 0; c < COLUMNS; c++) {
        unsigned h = bands[c] >> 6;
        m_heights[m_head][c] = h > MAX_HEIGHT ? MAX_HEIGHT : h;
    }
}

psyqo::Color WaterfallScene::heightColor(unsigned height, unsigned age) {
    // Dark blue at rest, through cyan and green to yellow at the peaks, and
    // fading as the row recedes.
    unsigned t = height * 255 / MAX_HEIGHT;
    unsigned r = t > 128 ? (t - 128) * 2 : 0;
    unsigned g = t < 128 ? t * 2 : 255;
    unsigned b = t < 128 ? 160 - t / 2 : 96 - (t - 128) * 3 / 4;
    unsigned fade = 256 - age * 8;
    return {{.r = uint8_t(r * fade >> 8), .g = uint8_t(g * fade >> 8), .b = uint8_t(b * fade >> 8)}};
}

void WaterfallScene::project() {
    // Row 0 is the newest, at z = 0; each older row is ROW_STEP further back.
    for (unsigned i = 0; i < VERTICES; i += 3) {
        int16_t xy[3][2];
        int16_t z[3];
        for (unsigned k = 0; k < 3; k++) {
            unsigned v = i + k;
            unsigned age = v / COLUMNS;
            unsigned c = v % COLUMNS;
            unsigned h = m_heights[(m_head + age) % ROWS][c];
            xy[k][0] = int16_t(c * COLUMN_STEP) - int16_t((COLUMNS - 1) * COLUMN_STEP / 2);
            xy[k][1] = -int16_t(h);
            z[k] = int16_t(age * ROW_STEP);
            m_colors[v] = heightColor(h, age);
        }
        psyqo::GTE::write<psyqo::GTE::Register::VXY0, psyqo::GTE::Unsafe>(uint16_t(xy[0][0]) | uint32_t(xy[0][1]) << 16);
        psyqo::GTE::write<psyqo::GTE::Register::VZ0, psyqo::GTE::Unsafe>(uint16_t(z[0]));
        psyqo::GTE::write<psyqo::GTE::Register::VXY1, psyqo::GTE::Unsafe>(uint16_t(xy[1][0]) | uint32_t(xy[1][1]) << 16);
        psyqo::GTE::write<psyqo::GTE::Register::VZ1, psyqo::GTE::Unsafe>(uint16_t(z[1]));
        psyqo::GTE::write<psyqo::GTE::Register::VXY2, psyqo::GTE::Unsafe>(uint16_t(xy[2][0]) | uint32_t(xy[2][1]) << 16);
        psyqo::GTE::write<psyqo::GTE::Register::VZ2, psyqo::GTE::Safe>(uint16_t(z[2]));
        psyqo::GTE::Kernels::rtpt();
        psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&m_screen[i].packed);
        psyqo::GTE::read<psyqo::GTE::Register::SXY1>(&m_screen[i + 1].packed);
        psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&m_screen[i + 2].packed);
    }
}

void WaterfallScene::frame() {
    pushRow();
    project();

    int parity = gpu().getParity();
    auto& ot = m_ots[parity];
    auto& clear = m_clear[parity];
    auto& quads = m_quads[parity];
    gpu().getNextClear(clear.primitive, {{.r = 0, .g = 0, .b = 16}});
    gpu().chain(clear);

    // Far rows go in the high buckets, which the GPU draws first.
    unsigned q = 0;
    for (unsigned age = 0; age < ROWS - 1; age++) {
        for (unsigned c = 0; c < COLUMNS - 1; c++) {
            unsigned a = age * COLUMNS + c;
            unsigned b = a + 1;
            unsigned d = a + COLUMNS;
            unsigned e = d + 1;
            auto& quad = quads[q++];
            quad.primitive.setPointA(m_screen[a]);
            quad.primitive.setPointB(m_screen[b]);
            quad.primitive.setPointC(m_screen[d]);
            quad.primitive.setPointD(m_screen[e]);
            quad.primitive.setColorA(m_colors[a]);
            quad.primitive.setColorB(m_colors[b]);
            quad.primitive.setColorC(m_colors[d]);
            quad.primitive.setColorD(m_colors[e]);
            quad.primitive.setOpaque();
            ot.insert(quad, age);
        }
    }
    gpu().chain(ot);

    auto& pos = waterfall.m_playbackLocation.relative;
    waterfall.m_font.chainprintf(gpu(), {{.x = 4, .y = 8}}, {.r = 0xff, .g = 0xff, .b = 0xff},
                                 "Track %i @%02i:%02i  halves %u", waterfall.m_playbackLocation.track, pos.m, pos.s,
                                 waterfall.m_capture.halves());
}

int main() { return waterfall.run(); }
