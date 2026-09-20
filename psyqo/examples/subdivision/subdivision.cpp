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

// A checkered ground plane, drawn four different ways, cycling on Cross.
//
// The point of the example is that all four look different, and that the
// differences are the entire argument for polygon subdivision:
//
//   Off              One quad per tile. The GPU maps textures affinely, so the
//                    checker bends along the diagonal each quad is split on.
//                    The nearer the tile, the worse it gets.
//   Fixed            Every tile divided to the same depth. The bending goes
//                    away, and so does the polygon budget.
//   Active           Divided only where it matters, by distance. The count
//                    comes back down, and cracks appear along every boundary
//                    between a divided tile and an undivided one.
//   Active + fill    The same frame with a triangle drawn over each crack.
//
// Up and Down move the distance at which Active stops dividing, so you can
// walk the boundary across the floor and watch the cracks move with it.

#include <stdint.h>

#include "psyqo/application.hh"
#include "psyqo/fixed-point.hh"
#include "psyqo/font.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/primitives/common.hh"
#include "psyqo/primitives/quads.hh"
#include "psyqo/primitives/triangles.hh"
#include "psyqo/scene.hh"
#include "psyqo/simplepad.hh"
#include "psyqo/soft-math.hh"
#include "psyqo/subdivision.hh"
#include "psyqo/trigonometry.hh"
#include "psyqo/vector.hh"

using namespace psyqo::fixed_point_literals;
using namespace psyqo::trig_literals;

namespace {

// The floor is a TILES_X by TILES_Z grid of quads lying in the XZ plane, with
// the camera at the origin looking down +Z.
//
// The grid is yawed by a few degrees, and that is not decoration. With the grid
// square to the camera, every tile edge is axis-aligned on screen, the midpoint
// of a divided edge projects to almost exactly the point the undivided
// neighbour interpolates, and the cracks this example exists to show very
// nearly do not happen - measured at a few scattered pixels across the whole
// floor. Axis alignment is the degenerate case. Turning the floor puts the
// rounding error back in general position, which is where a real scene lives.
constexpr unsigned TILES_X = 9;
constexpr unsigned TILES_Z = 7;
constexpr psyqo::Angle FLOOR_YAW = 0.08_pi;

constexpr psyqo::FixedPoint<> FLOOR_Y = 0.70_fp;   // camera height above the plane
constexpr psyqo::FixedPoint<> TILE_SIZE = 0.9_fp;  // tile edge, in world units
constexpr psyqo::FixedPoint<> NEAR_Z = 0.70_fp;   // front edge of the closest row

// Anything closer than this is dropped rather than projected. The GTE's
// perspective divide falls apart as z approaches zero, and a quad with one
// corner in that region does not come back as a slightly wrong quad, it comes
// back as a hole with a wildly displaced vertex. The deck calls this the near
// clipping problem and it is the second reason it gives for subdividing at all.
constexpr psyqo::FixedPoint<> MIN_Z = 0.25_fp;

constexpr unsigned MAX_DEPTH = 2;

// Worst case is every tile divided to MAX_DEPTH, which is 4^MAX_DEPTH leaves
// each. The fill triangles only ever appear on outer edges, so they are far
// fewer, but the arrays are sized to not have to think about it.
constexpr unsigned MAX_QUADS = TILES_X * TILES_Z * (1 << (2 * MAX_DEPTH));
constexpr unsigned MAX_TRIS = TILES_X * TILES_Z * 4 * (1 << MAX_DEPTH);

// The texture is 8bpp, 256x256 texels, which occupies 128x256 VRAM words. It
// goes at 512, 0, so the texture page is at pageX 8 (512 / 64), pageY 0.
constexpr unsigned TEX_VRAM_X = 512;
constexpr unsigned TEX_VRAM_Y = 0;
constexpr unsigned TEX_PAGE_X = TEX_VRAM_X / 64;
constexpr unsigned CLUT_VRAM_X = 512;
constexpr unsigned CLUT_VRAM_Y = 256;

enum class Mode : unsigned {
    Off,
    Fixed,
    Active,
    ActiveWithFill,
    Count,
};

const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Off:
            return "no subdivision";
        case Mode::Fixed:
            return "fixed subdivision";
        case Mode::Active:
            return "active, no fill";
        case Mode::ActiveWithFill:
            return "active + fill tris";
        default:
            return "?";
    }
}

}  // namespace

class SubdivisionDemo final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::Font<> m_font;
    psyqo::SimplePad m_input;
    psyqo::Trig<> m_trig;
};

class FloorScene final : public psyqo::Scene {
    void start(StartReason reason) override;
    void frame() override;

    void uploadCheckerboard();
    void drawFloor();

    // Projects a subdivision vertex through the GTE and fills in a primitive's
    // point and UV. The GTE holds three vertices at a time, so quads are done
    // as an rtpt of the first three plus an rtps of the fourth, exactly as the
    // cube example does it.
    template <typename Prim>
    bool projectQuad(const psyqo::Subdivision::TexturedVertex& a, const psyqo::Subdivision::TexturedVertex& b,
                     const psyqo::Subdivision::TexturedVertex& c, const psyqo::Subdivision::TexturedVertex& d,
                     Prim& prim);

    Mode m_mode = Mode::Off;
    psyqo::FixedPoint<> m_activeZ = 1.6_fp;
    unsigned m_quadCount = 0;
    unsigned m_triCount = 0;

    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> m_tpage[2];
    eastl::array<psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad>, MAX_QUADS> m_quads[2];
    eastl::array<psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedTriangle>, MAX_TRIS> m_tris[2];

    static constexpr psyqo::Color c_bg = {{.r = 0x10, .g = 0x10, .b = 0x20}};
};

static SubdivisionDemo demo;
static FloorScene floorScene;

void SubdivisionDemo::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
    m_font.uploadSystemFont(gpu());
}

void SubdivisionDemo::createScene() {
    m_input.initialize();
    pushScene(&floorScene);
}

// Builds an 8bpp checkerboard with a bright border on every cell. The borders
// are the part that matters: a flat checker shows the affine bend, but a thin
// straight line bending is impossible to argue with.
void FloorScene::uploadCheckerboard() {
    static uint16_t texture[128 * 256];

    for (unsigned y = 0; y < 256; y++) {
        for (unsigned x = 0; x < 256; x += 2) {
            auto texel = [](unsigned tx, unsigned ty) -> uint8_t {
                const unsigned cellX = tx & 31;
                const unsigned cellY = ty & 31;
                if ((cellX < 2) || (cellY < 2)) return 2;  // cell border
                return ((tx >> 5) ^ (ty >> 5)) & 1;        // checker
            };
            texture[y * 128 + (x >> 1)] = texel(x, y) | (texel(x + 1, y) << 8);
        }
    }

    psyqo::Rect texRegion = {.pos = {{.x = TEX_VRAM_X, .y = TEX_VRAM_Y}}, .size = {{.w = 128, .h = 256}}};
    gpu().uploadToVRAM(texture, texRegion);

    // Three entries used out of the 256 an 8bpp CLUT needs.
    static uint16_t clut[256];
    for (unsigned i = 0; i < 256; i++) clut[i] = 0;
    clut[0] = 0x2108;  // dark grey
    clut[1] = 0x6318;  // light grey
    clut[2] = 0x001f;  // red border

    psyqo::Rect clutRegion = {.pos = {{.x = CLUT_VRAM_X, .y = CLUT_VRAM_Y}}, .size = {{.w = 256, .h = 1}}};
    gpu().uploadToVRAM(clut, clutRegion);
}

void FloorScene::start(StartReason reason) {
    // The GTE never sees a rotation or a translation in this example, so the
    // only setup it needs is the screen offset, the projection distance, and
    // an identity rotation matrix.
    psyqo::GTE::clear<psyqo::GTE::Register::TRX, psyqo::GTE::Unsafe>();
    psyqo::GTE::clear<psyqo::GTE::Register::TRY, psyqo::GTE::Unsafe>();
    psyqo::GTE::clear<psyqo::GTE::Register::TRZ, psyqo::GTE::Unsafe>();

    psyqo::GTE::write<psyqo::GTE::Register::OFX, psyqo::GTE::Unsafe>(psyqo::FixedPoint<16>(160.0).raw());
    psyqo::GTE::write<psyqo::GTE::Register::OFY, psyqo::GTE::Unsafe>(psyqo::FixedPoint<16>(120.0).raw());
    psyqo::GTE::write<psyqo::GTE::Register::H, psyqo::GTE::Unsafe>(120);

    // A pure yaw. It is still an affine transform, so midpoints computed in
    // model space are exactly the midpoints of the transformed edges, and the
    // subdivision below stays correct without ever touching the matrix.
    auto yaw = psyqo::SoftMath::generateRotationMatrix33(FLOOR_YAW, psyqo::SoftMath::Axis::Y, demo.m_trig);
    psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::Rotation>(yaw);

    uploadCheckerboard();

    demo.m_input.setOnEvent([this](const psyqo::SimplePad::Event& event) {
        if (event.type != psyqo::SimplePad::Event::ButtonPressed) return;
        switch (event.button) {
            case psyqo::SimplePad::Button::Cross:
                m_mode = static_cast<Mode>((static_cast<unsigned>(m_mode) + 1) %
                                           static_cast<unsigned>(Mode::Count));
                break;
            case psyqo::SimplePad::Button::Up:
                m_activeZ += 0.1_fp;
                break;
            case psyqo::SimplePad::Button::Down:
                if (m_activeZ > 0.2_fp) m_activeZ -= 0.1_fp;
                break;
            default:
                break;
        }
    });
}

template <typename Prim>
bool FloorScene::projectQuad(const psyqo::Subdivision::TexturedVertex& a,
                             const psyqo::Subdivision::TexturedVertex& b,
                             const psyqo::Subdivision::TexturedVertex& c,
                             const psyqo::Subdivision::TexturedVertex& d, Prim& prim) {
    psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V0>(a.position);
    psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V1>(b.position);
    psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V2>(c.position);
    psyqo::GTE::Kernels::rtpt();

    psyqo::Vertex projected[4];
    psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&projected[0].packed);

    psyqo::GTE::writeSafe<psyqo::GTE::PseudoRegister::V0>(d.position);
    psyqo::GTE::Kernels::rtps();

    psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&projected[1].packed);
    psyqo::GTE::read<psyqo::GTE::Register::SXY1>(&projected[2].packed);
    psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&projected[3].packed);

    prim.pointA = projected[0];
    prim.pointB = projected[1];
    prim.pointC = projected[2];
    prim.pointD = projected[3];
    prim.uvA.u = a.uv.u;
    prim.uvA.v = a.uv.v;
    prim.uvB.u = b.uv.u;
    prim.uvB.v = b.uv.v;
    prim.uvC.u = c.uv.u;
    prim.uvC.v = c.uv.v;
    prim.uvD.u = d.uv.u;
    prim.uvD.v = d.uv.v;
    return true;
}

void FloorScene::drawFloor() {
    const int parity = gpu().getParity();
    auto& quads = m_quads[parity];
    auto& tris = m_tris[parity];

    m_quadCount = 0;
    m_triCount = 0;

    const unsigned depth = (m_mode == Mode::Off) ? 0 : MAX_DEPTH;
    const bool wantFill = (m_mode == Mode::ActiveWithFill);
    const bool active = (m_mode == Mode::Active) || wantFill;
    const auto activeZ = m_activeZ;

    // Fixed mode divides everything; active mode divides only what is close
    // enough that the affine error is worth paying for. The vertex nearest the
    // camera decides, which is what makes the boundary a clean line across the
    // floor rather than a ragged one.
    auto decide = [active, activeZ](const psyqo::Subdivision::TexturedVertex& a,
                                    const psyqo::Subdivision::TexturedVertex& b,
                                    const psyqo::Subdivision::TexturedVertex& c,
                                    const psyqo::Subdivision::TexturedVertex& d, unsigned depth) -> bool {
        if (!active) return true;
        // Only the top-level call decides. A tile is either divided all the way
        // or not at all, so the mismatch across a boundary is a full 4:1 rather
        // than one level, which is both what a real scene does and what makes
        // the crack wide enough to see.
        if (depth != MAX_DEPTH) return true;
        auto nearest = a.position.z;
        if (b.position.z < nearest) nearest = b.position.z;
        if (c.position.z < nearest) nearest = c.position.z;
        if (d.position.z < nearest) nearest = d.position.z;
        return nearest < activeZ;
    };

    auto emitQuad = [this, &quads](const psyqo::Subdivision::TexturedVertex& a,
                                   const psyqo::Subdivision::TexturedVertex& b,
                                   const psyqo::Subdivision::TexturedVertex& c,
                                   const psyqo::Subdivision::TexturedVertex& d) {
        if (m_quadCount >= MAX_QUADS) return;
        if ((a.position.z < MIN_Z) || (b.position.z < MIN_Z) || (c.position.z < MIN_Z) ||
            (d.position.z < MIN_Z)) {
            return;
        }
        auto& frag = quads[m_quadCount++];
        projectQuad(a, b, c, d, frag.primitive);
        frag.primitive.setColor({{.r = 0x80, .g = 0x80, .b = 0x80}});
        frag.primitive.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = CLUT_VRAM_X, .y = CLUT_VRAM_Y}});
        frag.primitive.tpage.setPageX(TEX_PAGE_X)
            .setPageY(0)
            .set(psyqo::Prim::TPageAttr::Tex8Bits)
            .enableDisplayArea();
        gpu().chain(frag);
    };

    auto emitFill = [this, wantFill, &tris](const psyqo::Subdivision::TexturedVertex& v0,
                                            const psyqo::Subdivision::TexturedVertex& mid,
                                            const psyqo::Subdivision::TexturedVertex& v1) {
        if (!wantFill) return;
        if (m_triCount >= MAX_TRIS) return;
        auto& frag = tris[m_triCount++];
        auto& prim = frag.primitive;

        psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V0>(v0.position);
        psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V1>(mid.position);
        psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V2>(v1.position);
        psyqo::GTE::Kernels::rtpt();

        psyqo::Vertex p[3];
        psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&p[0].packed);
        psyqo::GTE::read<psyqo::GTE::Register::SXY1>(&p[1].packed);
        psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&p[2].packed);

        prim.pointA = p[0];
        prim.pointB = p[1];
        prim.pointC = p[2];
        prim.uvA.u = v0.uv.u;
        prim.uvA.v = v0.uv.v;
        prim.uvB.u = mid.uv.u;
        prim.uvB.v = mid.uv.v;
        prim.uvC.u = v1.uv.u;
        prim.uvC.v = v1.uv.v;
        prim.setColor({{.r = 0x80, .g = 0x80, .b = 0x80}});
        prim.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = CLUT_VRAM_X, .y = CLUT_VRAM_Y}});
        prim.tpage.setPageX(TEX_PAGE_X).setPageY(0).set(psyqo::Prim::TPageAttr::Tex8Bits).enableDisplayArea();
        gpu().chain(frag);
    };

    // Far to near. The floor is a plane seen from above, so nothing on it can
    // occlude anything else on it and no ordering table is needed - but a fill
    // triangle has to be chained after the quads whose crack it covers, and
    // walking the grid in this order gets that for free.
    for (unsigned row = TILES_Z; row-- > 0;) {
        for (unsigned col = 0; col < TILES_X; col++) {
            const auto x0 = TILE_SIZE * (int32_t(col) - int32_t(TILES_X) / 2);
            const auto x1 = x0 + TILE_SIZE;
            const auto z0 = NEAR_Z + TILE_SIZE * int32_t(row);
            const auto z1 = z0 + TILE_SIZE;

            // A and B are the far edge, C and D the near one, which is the Z
            // order the GPU wants. Every tile carries the whole texture, so
            // the checker repeats per tile and the midpoints land on 128.
            const psyqo::Subdivision::TexturedVertex a = {{.x = x0, .y = FLOOR_Y, .z = z1}, {.u = 0, .v = 0}};
            const psyqo::Subdivision::TexturedVertex b = {{.x = x1, .y = FLOOR_Y, .z = z1}, {.u = 254, .v = 0}};
            const psyqo::Subdivision::TexturedVertex c = {{.x = x0, .y = FLOOR_Y, .z = z0}, {.u = 0, .v = 254}};
            const psyqo::Subdivision::TexturedVertex d = {{.x = x1, .y = FLOOR_Y, .z = z0}, {.u = 254, .v = 254}};

            psyqo::Subdivision::divideQuad(a, b, c, d, depth, psyqo::Subdivision::AllEdges, decide, emitQuad,
                                           emitFill);
        }
    }
}

void FloorScene::frame() {
    const int parity = gpu().getParity();
    auto& clear = m_clear[parity];

    gpu().getNextClear(clear.primitive, c_bg);
    gpu().chain(clear);

    drawFloor();

    demo.m_font.chainprintf(gpu(), {{.x = 4, .y = 4}}, {{.r = 0xff, .g = 0xff, .b = 0xff}}, "%s",
                            modeName(m_mode));
    demo.m_font.chainprintf(gpu(), {{.x = 4, .y = 20}}, {{.r = 0xc0, .g = 0xc0, .b = 0xc0}},
                            "quads %u  fill tris %u", m_quadCount, m_triCount);
    demo.m_font.chainprintf(gpu(), {{.x = 4, .y = 36}}, {{.r = 0xc0, .g = 0xc0, .b = 0xc0}},
                            "cross: mode   up/down: stop distance");
}

int main() { return demo.run(); }
