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

// Phase-8 expected hardware-truth values for 4-vert textured quad
// rasterization. All values HW_VERIFIED on SCPH-5501.
//
// Findings worth flagging:
//
//   1. Top-left rule applies uniformly: right-vertex column and
//      bottom-row pixels of axis-aligned quads are NOT drawn -
//      sentinel reads back. (4,0) of a 5x4 quad reads 0xDEAD; (0,0)
//      of a parallelogram whose apex is a single point reads 0xDEAD.
//      The right vertex of every quad row is excluded.
//
//   2. GP0(0x2E) semi-trans does NOT blend when the texture's mask
//      bit is clear. PS1 hardware gates semi-trans blending on bit 15
//      of the sampled texel (texture-mask-bit). All our fixture
//      textures encode bit 15 = 0, so QFS4 / QFS8 read back the SAME
//      values as their opaque QFA counterparts. The semi-trans CODE
//      PATH is exercised, but the blend doesn't trigger - the soft
//      renderer's drawPoly4TEx*_S bodies must replicate this gating.
//
//   3. 3-vert / 4-vert path convergence at degenerate quads: a 4-vert
//      quad with v3 == v2 produces interior pixels identical to a
//      3-vert reference draw at every probed position. The 4-vert
//      math collapses correctly when the fourth vertex is redundant.

#include "raster-helpers.h"
#include "texture-fixtures.h"

// --------------------------------------------------------------------------
// QFA4: axis-aligned 16x8 4-bit quad
// --------------------------------------------------------------------------

#define QFA4_0_0     0x03e0u  /* CLUT4[0]  = vram555(0,  31, 0) */
#define QFA4_14_0    0x022eu  /* CLUT4[14] = vram555(14, 17, 0) */
#define QFA4_0_6     0x03e0u
#define QFA4_14_6    0x022eu
#define QFA4_7_3     0x0307u  /* CLUT4[7]  = vram555(7,  24, 0) */
#define QFA4_3_5     0x0383u  /* CLUT4[3]  = vram555(3,  28, 0) */

// --------------------------------------------------------------------------
// QFA8: axis-aligned 32x8 8-bit quad. CLUT8[u] = vram555(u&31, (255-u)&31,
// (u>>5)&31). The formula's (255-u)&31 reduces mod 32 so e.g. u=30 gives
// (255-30)=225, 225&31=1 -> green channel = 1 not 31.
// --------------------------------------------------------------------------

#define QFA8_0_0     0x03e0u  /* CLUT8[0]  = vram555(0,  31, 0) */
#define QFA8_30_0    0x003eu  /* CLUT8[30] = vram555(30, 1,  0) */
#define QFA8_0_6     0x03e0u
#define QFA8_30_6    0x003eu
#define QFA8_15_3    0x020fu  /* CLUT8[15] = vram555(15, 16, 0) */
#define QFA8_22_5    0x0136u  /* CLUT8[22] = vram555(22, 9,  0) */

// --------------------------------------------------------------------------
// QFA15: axis-aligned 16x8 15-bit. texel(u, v) = vram555(u&31, v&31,
// (u+v)&31). (0,0) excluded by top-left rule - reads sentinel.
// --------------------------------------------------------------------------

#define QFA15_0_0    RASTER_SENTINEL  /* top-left vertex excluded */
#define QFA15_14_0   0x380eu  /* vram555(14, 0, 14) */
#define QFA15_0_6    0x18c0u  /* vram555(0,  6, 6)  */
#define QFA15_14_6   0x50ceu  /* vram555(14, 6, 20) - blue=20 overflows 5-bit at bit 14 */
#define QFA15_7_3    0x2867u  /* vram555(7,  3, 10) */
#define QFA15_3_5    0x20a3u  /* vram555(3,  5, 8)  */

// --------------------------------------------------------------------------
// QFD4: parallelogram-skewed 4-bit quad. UV interpolation along the
// slanted edges produces non-trivial per-pixel UV positions.
// --------------------------------------------------------------------------

#define QFD4_0_0     0x03e0u
#define QFD4_8_0     0x02e8u
#define QFD4_4_3     0x03a2u
#define QFD4_10_3    0x02e8u
#define QFD4_4_6     0x03c1u
#define QFD4_14_6    0x028bu

#define QFD15_0_0    RASTER_SENTINEL  /* apex excluded by top-left */
#define QFD15_8_0    0x2008u
#define QFD15_4_3    0x1462u
#define QFD15_10_3   0x2c68u
#define QFD15_4_6    0x1cc1u
#define QFD15_14_6   0x44cbu

// --------------------------------------------------------------------------
// QFO[4|8|15]: 5x4 axis-aligned quad. The right-vertex column (x=4) is
// EXCLUDED by hardware's top-left rule across all depths - reads
// sentinel at every row. The "terminal sampler" question the audit
// raised doesn't surface as a hardware-visible pixel at the
// right-vertex column; it only matters for soft renderer pixels
// hardware excludes anyway. Note: with hardware's `(rightX-1)>>16`
// rule the LAST drawn column is x=3, and the texture coords at that
// pixel match a standard per-pixel UV sample - no terminal-pair
// asymmetry to characterize against hardware.
// --------------------------------------------------------------------------

#define QFO4_TERMINAL_4_0    RASTER_SENTINEL
#define QFO4_TERMINAL_4_1    RASTER_SENTINEL
#define QFO4_TERMINAL_4_2    RASTER_SENTINEL
#define QFO4_INTERIOR_2_1    0x03a2u  /* CLUT4[2] = vram555(2, 29, 0) */
#define QFO4_INTERIOR_3_2    0x0383u  /* CLUT4[3] = vram555(3, 28, 0) */

#define QFO8_TERMINAL_4_0    RASTER_SENTINEL
#define QFO8_TERMINAL_4_1    RASTER_SENTINEL
#define QFO8_TERMINAL_4_2    RASTER_SENTINEL
#define QFO8_INTERIOR_2_1    0x03a2u  /* CLUT8[2] = vram555(2, 29, 0) */

#define QFO15_TERMINAL_4_0   RASTER_SENTINEL
#define QFO15_TERMINAL_4_1   RASTER_SENTINEL
#define QFO15_TERMINAL_4_2   RASTER_SENTINEL
#define QFO15_INTERIOR_2_1   0x0c22u  /* vram555(2, 1, 3) */

// --------------------------------------------------------------------------
// QFS4 / QFS8: semi-trans. PS1 hardware gates the blend on the
// texture's bit-15 mask. Our fixture textures encode bit 15 = 0 in
// every texel/CLUT entry, so the semi-trans command produces the same
// values as the opaque command - no blend applied.
// --------------------------------------------------------------------------

#define QFS4_0_0     0x03e0u  /* same as QFA4_0_0 - no blend */
#define QFS4_7_3     0x0307u  /* same as QFA4_7_3 */
#define QFS4_14_6    0x022eu  /* same as QFA4_14_6 */

#define QFS8_0_0     0x03e0u  /* same as QFA8_0_0 */
#define QFS8_15_3    0x020fu  /* same as QFA8_15_3 */
#define QFS8_30_6    0x003eu  /* same as QFA8_30_6 */

// --------------------------------------------------------------------------
// QFDEG / QFDEG_REF: degenerate 4-vert quad collapses to 3-vert
// triangle. Both produce identical pixels at every probed position.
// --------------------------------------------------------------------------

#define QFDEG_0_0    0x03e0u
#define QFDEG_7_3    0x0307u
#define QFDEG_3_5    0x0383u

#define QFDEG_REF_0_0    QFDEG_0_0  /* 3-vert ref matches 4-vert degenerate */
#define QFDEG_REF_7_3    QFDEG_7_3
#define QFDEG_REF_3_5    QFDEG_3_5
