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

// Renders the PXM2 meshes described in common/util/psxm.hh. Geometry only: the
// texture lives in a TIM and is loaded separately, so nothing here knows
// anything about images.

#pragma once

#include <stdint.h>

#include "common/util/psxm.hh"

#include "psyqo/fixed-point.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/matrix.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/common.hh"
#include "psyqo/primitives/triangles.hh"
#include "psyqo/soft-math.hh"
#include "psyqo/vector.hh"

namespace psxm {

static_assert(sizeof(psyqo::GTE::PackedVec3) == sizeof(Vec3), "PXM2 vertex layout mismatch");

// Scratch space and primitive storage. Templated so an application sizes it to
// the largest model it will actually show rather than to some arbitrary
// ceiling.
template <size_t MaxVertices, size_t MaxNormals, size_t MaxTriangles>
class Renderer {
  public:
    // Which way a front face winds after projection. A knob rather than an
    // assertion, because the answer depends on the exporter's handedness and is
    // cheaper to observe than to derive.
    enum class Culling { Normal, Inverted, Disabled };

    struct Stats {
        unsigned drawn;
        unsigned rejectedNear;
        unsigned rejectedBackface;
        unsigned rejectedRasterizer;
        unsigned rejectedDepth;
    };

    Stats stats() const { return m_stats; }
    void setCulling(Culling culling) { m_culling = culling; }
    Culling culling() const { return m_culling; }
    void setDepthShift(unsigned shift) { m_depthShift = shift; }

    // The GTE holds the projection, so this belongs in the scene's start(), and
    // again after anything else has borrowed the GTE.
    void setProjection(unsigned screenWidth, unsigned screenHeight, int32_t projectionDistance) {
        psyqo::GTE::write<psyqo::GTE::Register::OFX, psyqo::GTE::Unsafe>(
            psyqo::FixedPoint<16>(screenWidth / 2, 0).raw());
        psyqo::GTE::write<psyqo::GTE::Register::OFY, psyqo::GTE::Unsafe>(
            psyqo::FixedPoint<16>(screenHeight / 2, 0).raw());
        psyqo::GTE::write<psyqo::GTE::Register::H, psyqo::GTE::Safe>(projectionDistance);
    }

    // Camera in raw GTE units, matching the vertex data. The GTE computes
    // RT * v + TR and we want R * (v - camera), so the translation is the
    // rotated camera position, negated.
    void setCamera(const psyqo::Matrix33& view, int32_t x, int32_t y, int32_t z) {
        psyqo::Vec3 position = {psyqo::FixedPoint<>(x, psyqo::FixedPoint<>::RAW),
                                psyqo::FixedPoint<>(y, psyqo::FixedPoint<>::RAW),
                                psyqo::FixedPoint<>(z, psyqo::FixedPoint<>::RAW)};
        psyqo::Vec3 translated;
        psyqo::SoftMath::matrixVecMul3(view, position, &translated);
        psyqo::GTE::write<psyqo::GTE::Register::TRX, psyqo::GTE::Unsafe>(-translated.x.raw());
        psyqo::GTE::write<psyqo::GTE::Register::TRY, psyqo::GTE::Unsafe>(-translated.y.raw());
        psyqo::GTE::write<psyqo::GTE::Register::TRZ, psyqo::GTE::Unsafe>(-translated.z.raw());
        psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::Rotation>(view);
    }

    // Directional light, evaluated once per unique normal into a table the
    // triangle loop then indexes, so the cost tracks the normal count rather
    // than the corner count.
    void computeShading(const Model& model, const psyqo::Vec3& lightDirection, psyqo::Color ambient,
                        psyqo::Color diffuse) {
        int32_t lx = lightDirection.x.raw();
        int32_t ly = lightDirection.y.raw();
        int32_t lz = lightDirection.z.raw();
        if (!fits(model)) return;
        unsigned count = model.normalCount();
        for (unsigned i = 0; i < count; i++) {
            const auto& n = model.normals()[i];
            int32_t intensity = (n.x * lx + n.y * ly + n.z * lz) >> 12;
            if (intensity < 0) intensity = 0;
            if (intensity > 4096) intensity = 4096;
            m_shading[i].r = uint8_t(ambient.r + (((diffuse.r - ambient.r) * intensity) >> 12));
            m_shading[i].g = uint8_t(ambient.g + (((diffuse.g - ambient.g) * intensity) >> 12));
            m_shading[i].b = uint8_t(ambient.b + (((diffuse.b - ambient.b) * intensity) >> 12));
        }
    }

    // Transforms every vertex once, then walks the triangles. With roughly as
    // many vertices as triangles, going indexed saves about two thirds of the
    // projection work.
    template <size_t N, psyqo::Safe Safety>
    unsigned render(const Model& model, psyqo::OrderingTable<N, Safety>& ot, unsigned parity,
                    psyqo::PrimPieces::TPageAttr tpage, psyqo::PrimPieces::ClutIndex clut) {
        m_stats = {};
        // A model larger than this renderer was sized for draws nothing, which
        // is easier to notice than a model drawn from half-filled scratch space.
        if (!fits(model)) return 0;
        project(model);

        auto& fragments = m_fragments[parity];
        unsigned count = model.triangleCount();
        unsigned drawn = 0;

        for (unsigned i = 0; i < count; i++) {
            const auto& tri = model.triangles()[i];
            unsigned a = tri.vertices[0];
            unsigned b = tri.vertices[1];
            unsigned c = tri.vertices[2];

            // Anything at or behind the projection plane came out of the GTE as
            // garbage, so drop it rather than draw it wrong.
            int32_t za = m_z[a];
            int32_t zb = m_z[b];
            int32_t zc = m_z[c];
            if (za < c_nearThreshold || zb < c_nearThreshold || zc < c_nearThreshold) {
                m_stats.rejectedNear++;
                continue;
            }

            const psyqo::Vertex& pa = m_projected[a];
            const psyqo::Vertex& pb = m_projected[b];
            const psyqo::Vertex& pc = m_projected[c];

            // Backface test as a plain 2D cross product on positions we already
            // hold, rather than pushing all three back into the GTE to ask it
            // about a winding we can compute here.
            int32_t abx = pb.x - pa.x;
            int32_t aby = pb.y - pa.y;
            int32_t acx = pc.x - pa.x;
            int32_t acy = pc.y - pa.y;
            int32_t winding = abx * acy - acx * aby;
            if (m_culling == Culling::Normal && winding <= 0) {
                m_stats.rejectedBackface++;
                continue;
            }
            if (m_culling == Culling::Inverted && winding >= 0) {
                m_stats.rejectedBackface++;
                continue;
            }

            // The GPU drops any triangle spanning more than 1023 horizontally or
            // 511 vertically, so one that big is not worth building.
            if (!withinRasterizerLimits(pa, pb, pc)) {
                m_stats.rejectedRasterizer++;
                continue;
            }

            int32_t z = ((za + zb + zc) / 3) >> m_depthShift;
            if (z < 1) z = 1;
            if (z >= int32_t(N)) {
                m_stats.rejectedDepth++;
                continue;
            }

            auto& fragment = fragments[drawn];
            auto& prim = fragment.primitive;
            prim.pointA = pa;
            prim.pointB = pb;
            prim.pointC = pc;
            prim.uvA.u = tri.uv[0].u;
            prim.uvA.v = tri.uv[0].v;
            prim.uvB.u = tri.uv[1].u;
            prim.uvB.v = tri.uv[1].v;
            prim.uvC.u = tri.uv[2].u;
            prim.uvC.v = tri.uv[2].v;
            prim.clutIndex = clut;
            prim.tpage = tpage;
            prim.setColorA(m_shading[tri.normals[0]]);
            prim.colorB = m_shading[tri.normals[1]];
            prim.colorC = m_shading[tri.normals[2]];
            prim.setOpaque();

            ot.insert(fragment, z);
            if (++drawn == MaxTriangles) break;
        }

        m_stats.drawn = drawn;
        return drawn;
    }

    // Whether the model fits the scratch space this renderer was sized for.
    static bool fits(const Model& model) {
        return model.vertexCount() <= MaxVertices && model.normalCount() <= MaxNormals &&
               model.triangleCount() <= MaxTriangles;
    }

  private:
    static constexpr int32_t c_nearThreshold = 8;

    void project(const Model& model) {
        unsigned count = model.vertexCount();
        // The GTE takes the same three packed int16 the format stores.
        auto vertices = reinterpret_cast<const psyqo::GTE::PackedVec3*>(model.vertices());
        unsigned i = 0;

        // The GTE transforms three vertices per rtpt, hence the stride, with
        // whatever is left over going through one at a time below.
        for (; i + 3 <= count; i += 3) {
            psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V0>(vertices[i + 0]);
            psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::V1>(vertices[i + 1]);
            psyqo::GTE::writeSafe<psyqo::GTE::PseudoRegister::V2>(vertices[i + 2]);
            psyqo::GTE::Kernels::rtpt();
            psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&m_projected[i + 0].packed);
            psyqo::GTE::read<psyqo::GTE::Register::SXY1>(&m_projected[i + 1].packed);
            psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&m_projected[i + 2].packed);
            uint32_t z;
            psyqo::GTE::read<psyqo::GTE::Register::SZ1>(&z);
            m_z[i + 0] = uint16_t(z);
            psyqo::GTE::read<psyqo::GTE::Register::SZ2>(&z);
            m_z[i + 1] = uint16_t(z);
            psyqo::GTE::read<psyqo::GTE::Register::SZ3>(&z);
            m_z[i + 2] = uint16_t(z);
        }

        for (; i < count; i++) {
            psyqo::GTE::writeSafe<psyqo::GTE::PseudoRegister::V0>(vertices[i]);
            psyqo::GTE::Kernels::rtps();
            psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&m_projected[i].packed);
            uint32_t z;
            psyqo::GTE::read<psyqo::GTE::Register::SZ3>(&z);
            m_z[i] = uint16_t(z);
        }
    }

    static bool withinRasterizerLimits(const psyqo::Vertex& a, const psyqo::Vertex& b,
                                       const psyqo::Vertex& c) {
        int32_t minX = a.x, maxX = a.x, minY = a.y, maxY = a.y;
        if (b.x < minX) minX = b.x;
        if (b.x > maxX) maxX = b.x;
        if (c.x < minX) minX = c.x;
        if (c.x > maxX) maxX = c.x;
        if (b.y < minY) minY = b.y;
        if (b.y > maxY) maxY = b.y;
        if (c.y < minY) minY = c.y;
        if (c.y > maxY) maxY = c.y;
        return (maxX - minX) < 1024 && (maxY - minY) < 512;
    }

    unsigned m_depthShift = 0;
    Culling m_culling = Culling::Normal;
    Stats m_stats = {};

    psyqo::Vertex m_projected[MaxVertices];
    uint16_t m_z[MaxVertices];
    psyqo::Color m_shading[MaxNormals];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::GouraudTexturedTriangle> m_fragments[2][MaxTriangles];
};

}  // namespace psxm
