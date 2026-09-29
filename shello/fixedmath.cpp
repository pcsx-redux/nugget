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

#include "shello/fixedmath.hh"

// These are direct ports of the original shell's math.c, with the bespoke
// dMul/dDiv replaced by FixedPoint<24>'s operators (which are bit-for-bit the
// same operations) and the cosine table replaced by Trig<24> (likewise
// identical). vs[i] is the i-th row of the matrix.

namespace shello {

void generateRotationMatrix3D(Matrix3D *m, psyqo::Angle t, Axis a, const Trig &trig) {
    Fixed s = trig.sin(t);
    Fixed c = trig.cos(t);
    Fixed zero(0.0);
    Fixed one(1.0);
    switch (a) {
        case Axis::X:
            m->vs[0].x = one;
            m->vs[0].y = zero;
            m->vs[0].z = zero;
            m->vs[1].x = zero;
            m->vs[1].y = c;
            m->vs[1].z = s;
            m->vs[2].x = zero;
            m->vs[2].y = -s;
            m->vs[2].z = c;
            break;
        case Axis::Y:
            m->vs[0].x = c;
            m->vs[0].y = zero;
            m->vs[0].z = -s;
            m->vs[1].x = zero;
            m->vs[1].y = one;
            m->vs[1].z = zero;
            m->vs[2].x = s;
            m->vs[2].y = zero;
            m->vs[2].z = c;
            break;
        case Axis::Z:
            m->vs[0].x = c;
            m->vs[0].y = s;
            m->vs[0].z = zero;
            m->vs[1].x = -s;
            m->vs[1].y = c;
            m->vs[1].z = zero;
            m->vs[2].x = zero;
            m->vs[2].y = zero;
            m->vs[2].z = one;
            break;
    }
}

void multiplyMatrix3D(const Matrix3D *m1, const Matrix3D *m2, Matrix3D *out) {
    Fixed x0 = m1->vs[0].x * m2->vs[0].x + m1->vs[1].x * m2->vs[0].y + m1->vs[2].x * m2->vs[0].z;
    Fixed y0 = m1->vs[0].y * m2->vs[0].x + m1->vs[1].y * m2->vs[0].y + m1->vs[2].y * m2->vs[0].z;
    Fixed z0 = m1->vs[0].z * m2->vs[0].x + m1->vs[1].z * m2->vs[0].y + m1->vs[2].z * m2->vs[0].z;
    Fixed x1 = m1->vs[0].x * m2->vs[1].x + m1->vs[1].x * m2->vs[1].y + m1->vs[2].x * m2->vs[1].z;
    Fixed y1 = m1->vs[0].y * m2->vs[1].x + m1->vs[1].y * m2->vs[1].y + m1->vs[2].y * m2->vs[1].z;
    Fixed z1 = m1->vs[0].z * m2->vs[1].x + m1->vs[1].z * m2->vs[1].y + m1->vs[2].z * m2->vs[1].z;
    Fixed x2 = m1->vs[0].x * m2->vs[2].x + m1->vs[1].x * m2->vs[2].y + m1->vs[2].x * m2->vs[2].z;
    Fixed y2 = m1->vs[0].y * m2->vs[2].x + m1->vs[1].y * m2->vs[2].y + m1->vs[2].y * m2->vs[2].z;
    Fixed z2 = m1->vs[0].z * m2->vs[2].x + m1->vs[1].z * m2->vs[2].y + m1->vs[2].z * m2->vs[2].z;

    out->vs[0].x = x0;
    out->vs[0].y = y0;
    out->vs[0].z = z0;
    out->vs[1].x = x1;
    out->vs[1].y = y1;
    out->vs[1].z = z1;
    out->vs[2].x = x2;
    out->vs[2].y = y2;
    out->vs[2].z = z2;
}

void scaleMatrix3D(Matrix3D *m, Fixed s) {
    for (unsigned i = 0; i < 3; i++) {
        m->vs[i].x = m->vs[i].x * s;
        m->vs[i].y = m->vs[i].y * s;
        m->vs[i].z = m->vs[i].z * s;
    }
}

void matrixVertexMul3D(const Matrix3D *m, const Vec3 *v, Vec3 *out) {
    Fixed x = v->x;
    Fixed y = v->y;
    Fixed z = v->z;
    out->x = x * m->vs[0].x + y * m->vs[0].y + z * m->vs[0].z;
    out->y = x * m->vs[1].x + y * m->vs[1].y + z * m->vs[1].z;
    out->z = x * m->vs[2].x + y * m->vs[2].y + z * m->vs[2].z;
}

void matrixVertexMul3Dxy(const Matrix3D *m, const Vec3 *v, Vec2 *out) {
    Fixed x = v->x;
    Fixed y = v->y;
    Fixed z = v->z;
    out->x = x * m->vs[0].x + y * m->vs[0].y + z * m->vs[0].z;
    out->y = x * m->vs[1].x + y * m->vs[1].y + z * m->vs[1].z;
}

Fixed matrixVertexMul3Dz(const Matrix3D *m, const Vec3 *v) {
    Fixed x = v->x;
    Fixed y = v->y;
    Fixed z = v->z;
    return x * m->vs[2].x + y * m->vs[2].y + z * m->vs[2].z;
}

}  // namespace shello
