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

#include "psyqo/fixed-point.hh"
#include "psyqo/primitives/common.hh"
#include "psyqo/trigonometry.hh"
#include "psyqo/vector.hh"

// The OpenBIOS shell does all of its 3D in a bespoke 8.24 fixed point format,
// with ONE == 2^24, angles measured in units of 2048 to the full turn, and a
// cosine table seeded by the same recurrence psyqo's Trig uses. That maps
// exactly onto psyqo's templated fixed point: FixedPoint<24> is the 8.24 type,
// Trig<24> produces bit-identical cosine values, and Angle (FixedPoint<10>,
// fractions of Pi) measures 2048 raw to the full turn just like the shell's
// DC_2PI. We deliberately stay off the GTE: this is the same software pipeline
// the original used, expressed through psyqo's types.

namespace shello {

// 8.24 fixed point. ONE (the original's 16777216) is Fixed(1.0).
typedef psyqo::FixedPoint<24> Fixed;
typedef psyqo::Vector<3, 24> Vec3;
typedef psyqo::Vector<2, 24> Vec2;
typedef psyqo::Trig<24> Trig;

enum class Axis { X, Y, Z };

// A 3x3 matrix whose rows are vs[0..2], matching the original Matrix3D layout
// (vs[i] is the i-th row, so vs[0].x/y/z is the first row).
struct Matrix3D {
    Vec3 vs[3];
};

// The shell stores rotation phase in the 8.24 domain ([0, ONE) == one full
// turn) and feeds the rotation routines an angle pre-shifted to DC units via
// `phase >> 13` (since ONE / 2048 == 2^13). That DC value is exactly the raw
// value of a psyqo Angle (FixedPoint<10>, 2048 raw to the turn).
static inline psyqo::Angle dcToAngle(int32_t dc) { return psyqo::Angle(dc, psyqo::Angle::RAW); }
static inline psyqo::Angle phaseToAngle(Fixed phase) { return dcToAngle(phase.raw() >> 13); }

void generateRotationMatrix3D(Matrix3D *m, psyqo::Angle t, Axis a, const Trig &trig);
void multiplyMatrix3D(const Matrix3D *m1, const Matrix3D *m2, Matrix3D *out);
void scaleMatrix3D(Matrix3D *m, Fixed s);
void matrixVertexMul3D(const Matrix3D *m, const Vec3 *v, Vec3 *out);
void matrixVertexMul3Dxy(const Matrix3D *m, const Vec3 *v, Vec2 *out);
Fixed matrixVertexMul3Dz(const Matrix3D *m, const Vec3 *v);

static inline void rotationMatrix2D(Matrix3D *m, psyqo::Angle t, const Trig &trig) {
    Fixed c = trig.cos(t);
    Fixed s = trig.sin(t);
    m->vs[0].x = c;
    m->vs[0].y = s;
    m->vs[1].x = -s;
    m->vs[1].y = c;
}

// 2x2 transform (rotation/scale) applied to a 2D vector, packed into the top
// two rows of a Matrix3D's x/y, mirroring the original Matrix2D usage.
static inline void matrixVertexMul2D(const Matrix3D *m, Vec2 *v) {
    Fixed x = v->x;
    Fixed y = v->y;
    v->x = x * m->vs[0].x + y * m->vs[0].y;
    v->y = x * m->vs[1].x + y * m->vs[1].y;
}

// Lerp helpers, matching the original's three integer flavors plus color.
//   p in [0, 256] for the U/S/C variants, p in [0, ONE] for the D variant.
static inline uint32_t lerpU(uint32_t s, uint32_t d, unsigned p) { return (s * (256 - p) + d * p) >> 8; }
static inline int32_t lerpS(int32_t s, int32_t d, unsigned p) { return (s * (256 - p) + d * p) >> 8; }
static inline Fixed lerpD(Fixed s, Fixed d, Fixed p) {
    Fixed one(1.0);
    return s * (one - p) + d * p;
}
static inline psyqo::Color lerpC(const psyqo::Color s, const psyqo::Color d, unsigned p) {
    psyqo::Color r;
    r.r = lerpU(s.r, d.r, p);
    r.g = lerpU(s.g, d.g, p);
    r.b = lerpU(s.b, d.b, p);
    return r;
}

}  // namespace shello
