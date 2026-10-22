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

#pragma once

#include <stdint.h>

/**
 * @file psxm.hh
 * @brief The PXM2 mesh format, and a reader for it.
 *
 * @details PXM2 is what the `nugget.psxm` xmake rule (tools/xmake/assets.lua)
 * produces from a Wavefront OBJ: a triangle mesh in the units the GTE and the
 * GPU take directly, so the runtime only points into it. Textures are not part
 * of it; they travel separately, typically as TIMs.
 *
 * All fields are little endian. The layout is:
 *
 * | Offset           | Size       | Contents                                   |
 * |------------------|------------|--------------------------------------------|
 * | 0                | 24         | `Header`                                   |
 * | `vertexOffset`   | 6 * count  | vertices, three int16 x, y, z each         |
 * | `normalOffset`   | 6 * count  | normals, three int16 x, y, z each, 4.12    |
 * | `triangleOffset` | 20 * count | `Triangle` records                         |
 *
 * The offsets are relative to the start of the blob and each section starts on
 * a four byte boundary; the bytes between sections are zero. Readers find the
 * sections through the offsets, never by assuming they follow each other, so a
 * section added later does not move the existing ones.
 *
 * Coordinates are in the converter's fixed-point scale (OBJ units times the
 * rule's `nugget.psxm.scale`, 256 by default), with Y negated from the OBJ so
 * that it grows downwards like the screen. Normals take the same Y flip and are
 * scaled by 4096; identical normals are stored once. Corner order is kept as
 * authored, except that a face whose winding disagrees with its own authored
 * normal is reordered to match it. Front faces then wind positively once
 * projected.
 *
 * Each triangle holds three vertex indices, three normal indices (one per
 * corner, so smooth and hard edges both work), and three texture coordinates
 * in texels, with V flipped from the OBJ because VRAM grows downwards. Indices
 * are zero based and always below the matching count.
 */

namespace psxm {

struct Header {
    char magic[4];  ///< "PXM2"
    uint16_t vertexCount;
    uint16_t normalCount;
    uint16_t triangleCount;
    uint16_t reserved;  ///< Zero.
    uint32_t vertexOffset;
    uint32_t normalOffset;
    uint32_t triangleOffset;
};
static_assert(sizeof(Header) == 24, "PXM2 header size mismatch");

struct Vec3 {
    int16_t x, y, z;
};
static_assert(sizeof(Vec3) == 6, "PXM2 vector size mismatch");

struct UV {
    uint8_t u, v;
};

struct Triangle {
    uint16_t vertices[3];
    uint16_t normals[3];
    UV uv[3];
    uint16_t padding;  ///< Zero. Keeps the record a multiple of four bytes.
};
static_assert(sizeof(Triangle) == 20, "PXM2 triangle size mismatch");

/**
 * @brief Points into a PXM2 blob without copying it, so the blob has to outlive
 * it. For an asset linked in by the build rule that costs nothing.
 */
class Model {
  public:
    /**
     * @brief Accepts the blob if it carries the PXM2 magic. The blob has to be
     * four byte aligned.
     */
    bool parse(const uint8_t* blob) {
        auto header = reinterpret_cast<const Header*>(blob);
        if (header->magic[0] != 'P' || header->magic[1] != 'X' || header->magic[2] != 'M' ||
            header->magic[3] != '2') {
            return false;
        }
        m_header = header;
        m_vertices = reinterpret_cast<const Vec3*>(blob + header->vertexOffset);
        m_normals = reinterpret_cast<const Vec3*>(blob + header->normalOffset);
        m_triangles = reinterpret_cast<const Triangle*>(blob + header->triangleOffset);
        return true;
    }

    unsigned vertexCount() const { return m_header->vertexCount; }
    unsigned normalCount() const { return m_header->normalCount; }
    unsigned triangleCount() const { return m_header->triangleCount; }

    const Vec3* vertices() const { return m_vertices; }
    const Vec3* normals() const { return m_normals; }
    const Triangle* triangles() const { return m_triangles; }

  private:
    const Header* m_header = nullptr;
    const Vec3* m_vertices = nullptr;
    const Vec3* m_normals = nullptr;
    const Triangle* m_triangles = nullptr;
};

}  // namespace psxm
