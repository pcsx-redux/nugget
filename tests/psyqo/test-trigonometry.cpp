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

#include "psyqo/trigonometry.hh"

#include <EASTL/array.h>
#include <stdint.h>

#include "snitch_all.hpp"

using namespace psyqo;
using namespace psyqo::trig_literals;

static Trig<> trig;
static Trig<24> trig24;

// cos(n * 2pi / 2048) * 2^34 for n in [0, 2048], computed at compile time.
static consteval eastl::array<int64_t, 2049> referenceCos() {
    eastl::array<int64_t, 2049> ref;
    for (int n = 0; n <= 2048; n++) {
        double x = n * (2.0 * 3.14159265358979323846 / 2048.0);
        double term = 1.0, sum = 1.0;
        for (int k = 1; k < 40; k++) {
            term *= -x * x / ((2 * k - 1) * (2 * k));
            sum += term;
        }
        double v = sum * 17179869184.0;
        ref[n] = v < 0 ? int64_t(v - 0.5) : int64_t(v + 0.5);
    }
    return ref;
}

static constexpr auto s_reference = referenceCos();

// Returns the largest and the summed absolute error over all 2048 angles, in
// units of 2^-10 of the table's last bit.
template <unsigned precisionBits>
static void sweep(const Trig<precisionBits>& t, int64_t& maxErr, int64_t& sumErr) {
    maxErr = 0;
    sumErr = 0;
    for (unsigned n = 0; n < 2048; n++) {
        Angle a;
        a.value = n;
        int64_t got = int64_t(t.cos(a).raw()) << (34 - precisionBits);
        int64_t err = (got - s_reference[n]) >> (24 - precisionBits);
        if (err < 0) err = -err;
        if (err > maxErr) maxErr = err;
        sumErr += err;
    }
}

// --- Cosine at key angles ---

TEST_CASE("cos(0) = 1") {
    auto c = trig.cos(0.0_pi);
    REQUIRE(c.raw() == (1 << 12));
}

TEST_CASE("cos(pi/2) = 0") {
    auto c = trig.cos(0.5_pi);
    REQUIRE(c.raw() == 0);
}

TEST_CASE("cos(pi) = -1") {
    auto c = trig.cos(1.0_pi);
    REQUIRE(c.raw() == -(1 << 12));
}

TEST_CASE("cos(3pi/2) = 0") {
    auto c = trig.cos(1.5_pi);
    REQUIRE(c.raw() == 0);
}

// --- Sine at key angles ---

TEST_CASE("sin(0) = 0") {
    auto s = trig.sin(0.0_pi);
    // sin(0) = cos(-pi/2) = cos(3pi/2) due to unsigned modular arithmetic
    REQUIRE(s.raw() == 0);
}

TEST_CASE("sin(pi/2) = 1") {
    auto s = trig.sin(0.5_pi);
    // sin(pi/2) = cos(0) = 1
    REQUIRE(s.raw() == (1 << 12));
}

TEST_CASE("sin(pi) close to 0") {
    auto s = trig.sin(1.0_pi);
    REQUIRE(s.raw() == 0);
}

// --- Symmetry properties ---

TEST_CASE("sin/cos Pythagorean identity") {
    // sin^2(x) + cos^2(x) should be close to 1 for various angles
    Angle angles[] = {0.0_pi, 0.25_pi, 0.5_pi, 0.75_pi, 1.0_pi, 1.25_pi, 1.5_pi, 1.75_pi};
    for (auto a : angles) {
        auto s = trig.sin(a);
        auto c = trig.cos(a);
        auto sum = s * s + c * c;
        // 1.0 in 20.12 = 4096. Each product truncates.
        auto diff = sum.raw() - (1 << 12);
        REQUIRE(diff >= -3);
        REQUIRE(diff <= 3);
    }
}

TEST_CASE("cos is even: cos(-x) = cos(x)") {
    // Due to unsigned modular, -x mod 2pi = 2pi - x
    for (unsigned n = 0; n < 2048; n++) {
        Angle a, b;
        a.value = n;
        b.value = 2048 - n;
        REQUIRE(trig.cos(a).raw() == trig.cos(b).raw());
    }
}

TEST_CASE("cos(pi - x) = -cos(x)") {
    for (unsigned n = 0; n <= 1024; n++) {
        Angle a, b;
        a.value = n;
        b.value = 1024 - n;
        REQUIRE(trig.cos(a).raw() == -trig.cos(b).raw());
    }
}

TEST_CASE("sin is odd: sin(2pi - x) = -sin(x)") {
    auto a = trig.sin(0.3_pi);
    auto b = trig.sin(1.7_pi);
    REQUIRE(a.raw() == -b.raw());
}

// --- Accuracy over the whole circle ---

TEST_CASE("cos error over all angles, 20.12") {
    int64_t maxErr, sumErr;
    sweep(trig, maxErr, sumErr);
    // Within 0.7 of the last bit everywhere, 0.3 on average.
    REQUIRE(maxErr <= 717);
    REQUIRE(sumErr <= 2048 * 307);
}

TEST_CASE("cos error over all angles, 8.24") {
    int64_t maxErr, sumErr;
    sweep(trig24, maxErr, sumErr);
    // Within 650 of the last bit everywhere, 250 on average.
    REQUIRE(maxErr <= 650 * 1024);
    REQUIRE(sumErr <= 2048 * 250 * 1024);
}

// --- Angle literal ---

TEST_CASE("Angle literal _pi") {
    Angle a = 1.0_pi;
    REQUIRE(a.raw() == 1024);
    Angle b = 0.5_pi;
    REQUIRE(b.raw() == 512);
    Angle c = 2.0_pi;
    REQUIRE(c.raw() == 2048);
}
