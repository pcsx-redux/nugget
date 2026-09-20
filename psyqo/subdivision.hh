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

#include "psyqo/primitives/common.hh"
#include "psyqo/vector.hh"

namespace psyqo {

/**
 * @brief Recursive polygon subdivision.
 *
 * @details The GPU maps textures affinely, so the texture coordinates it
 * interpolates across a primitive are linear in screen space rather than
 * correct in perspective. On a large primitive with a lot of depth variation -
 * a ground plane being the canonical case - that shows up as the texture
 * visibly bending along the diagonal the GPU splits each quad on. Cutting the
 * primitive into smaller ones before handing it over keeps each piece small
 * enough that the error stays under a pixel.
 *
 * The division happens in three dimensions here, on model-space vertices,
 * before anything is projected. Halving screen coordinates instead is cheaper
 * and does not work: the midpoint of a segment in screen space is not the
 * image of the midpoint in world space, so the sub-vertices get texture
 * coordinates that are wrong in exactly the way subdivision was supposed to
 * fix. Because the model-view transform is affine, midpoints computed in model
 * space survive it, and only the projection of each new vertex has to be paid
 * for.
 *
 * Two things follow from dividing adaptively, which is the only way to divide
 * that is worth the cycles. First, a tile that gets divided while its
 * neighbour does not leaves a T-junction: the new midpoint does not land
 * exactly on the neighbour's straight edge once both have been rounded to
 * integer screen coordinates, and a crack opens along the boundary. Second,
 * the fix is to draw a triangle over the crack, spanning the two original
 * endpoints and the midpoint that was inserted between them. This header emits
 * those on request, on any edge you declare to be an outer boundary.
 *
 * Within a single divided primitive there is never a crack, because every
 * shared midpoint is computed once and handed to both children.
 */
namespace Subdivision {

/**
 * @brief A model-space vertex and its texture coordinates.
 *
 * @details Everything in here gets averaged when an edge is split.
 */
struct TexturedVertex {
    Vec3 position;
    PrimPieces::UVCoords uv;
};

/**
 * @brief Which edges of a quad lie on an outer boundary.
 *
 * @details Vertices are in the GPU's order, so the four edges are AB and CD
 * running one way and AC and BD running the other. An edge marked here is one
 * whose neighbour may be at a different subdivision level, and is therefore an
 * edge that needs a fill triangle when it gets split. Marking every edge is
 * always safe; it costs one triangle per split edge and cannot produce a crack.
 */
enum Edge : unsigned {
    NoEdges = 0,
    EdgeAB = 1 << 0,
    EdgeBD = 1 << 1,
    EdgeCD = 1 << 2,
    EdgeAC = 1 << 3,
    AllEdges = EdgeAB | EdgeBD | EdgeCD | EdgeAC,
};

/**
 * @brief Midpoint of two vertices, in position and texture coordinates at once.
 */
[[nodiscard]] constexpr TexturedVertex half(const TexturedVertex& a, const TexturedVertex& b) {
    return TexturedVertex{
        .position = (a.position + b.position) / 2,
        .uv = {.u = static_cast<uint8_t>((static_cast<unsigned>(a.uv.u) + b.uv.u) / 2),
               .v = static_cast<uint8_t>((static_cast<unsigned>(a.uv.v) + b.uv.v) / 2)},
    };
}

/**
 * @brief Recursively divide a quad, in three dimensions.
 *
 * @details Vertices are in the GPU's order: A and B are one edge, C and D the
 * opposite one, so the quad reads as a Z rather than as a loop. Each level of
 * recursion splits the quad into four.
 *
 * @param a Quad vertex A.
 * @param b Quad vertex B.
 * @param c Quad vertex C.
 * @param d Quad vertex D.
 * @param depth Maximum remaining levels of division. Zero emits immediately.
 * ⚠ This recurses four ways and is bounded only by `depth` and `decide`: the
 * call stack reaches `depth` frames and a fully-divided quad produces `4^depth`
 * leaves, so depth 8 is already 65536 emits. The PS1 stack is small and there is
 * no guard here - pick `depth` from what the scene can afford, and prefer a
 * `decide` that stops on screen size over a large fixed depth.
 * @param edges Which of this quad's edges are outer boundaries, as an `Edge`
 * mask. Pass `AllEdges` for a tile whose neighbours divide independently, and
 * `NoEdges` if nothing next to it can ever be at a different level.
 * @param decide Called as `decide(a, b, c, d, depth)` before each split, and
 * returning false stops the recursion for that quad. This is where a distance
 * or screen-size test goes.
 * @param emitQuad Called as `emitQuad(a, b, c, d)` for every leaf quad.
 * @param emitFill Called as `emitFill(v0, mid, v1)` for every fill triangle,
 * with the two original endpoints and the midpoint inserted between them. Pass
 * something that does nothing to see what the cracks look like without it.
 */
template <typename Decide, typename EmitQuad, typename EmitFill>
void divideQuad(const TexturedVertex& a, const TexturedVertex& b, const TexturedVertex& c,
                const TexturedVertex& d, unsigned depth, unsigned edges, Decide&& decide, EmitQuad&& emitQuad,
                EmitFill&& emitFill) {
    if ((depth == 0) || !decide(a, b, c, d, depth)) {
        emitQuad(a, b, c, d);
        return;
    }

    const auto ab = half(a, b);
    const auto cd = half(c, d);
    const auto ac = half(a, c);
    const auto bd = half(b, d);
    const auto center = half(ab, cd);

    // Every edge of this quad that borders something which may not be divided
    // is about to gain a vertex the neighbour does not have. Cover each one.
    if (edges & EdgeAB) emitFill(a, ab, b);
    if (edges & EdgeBD) emitFill(b, bd, d);
    if (edges & EdgeCD) emitFill(c, cd, d);
    if (edges & EdgeAC) emitFill(a, ac, c);

    const unsigned next = depth - 1;
    divideQuad(a, ab, ac, center, next, edges & (EdgeAB | EdgeAC), decide, emitQuad, emitFill);
    divideQuad(ab, b, center, bd, next, edges & (EdgeAB | EdgeBD), decide, emitQuad, emitFill);
    divideQuad(ac, center, c, cd, next, edges & (EdgeAC | EdgeCD), decide, emitQuad, emitFill);
    divideQuad(center, bd, cd, d, next, edges & (EdgeBD | EdgeCD), decide, emitQuad, emitFill);
}

}  // namespace Subdivision

}  // namespace psyqo
