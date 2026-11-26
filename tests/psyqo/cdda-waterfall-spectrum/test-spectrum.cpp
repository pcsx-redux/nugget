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

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/syscalls/syscalls.h"
#include "snitch_all.hpp"
#include "spectrum.hh"

// Pins the contract in spectrum.hh: a 256-point DFT, no window, bin
// magnitude 2|X[k]|/256, band = largest bin in BAND_EDGES[b]..BAND_EDGES[b+1].
// Every tone here is centred on a bin, so a correct transform puts all of its
// energy in that one bin and nothing leaks into the other bands.

namespace {

using Spectrum::BAND_EDGES;
using Spectrum::BANDS;
using Spectrum::SAMPLES;

constexpr double PI = 3.14159265358979323846;

constexpr double taylorSin(double x) {
    // x in [-pi, pi]; enough terms for 16-bit accuracy.
    double term = x, sum = x;
    for (int n = 1; n < 12; n++) {
        term *= -x * x / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

struct SineTable {
    int32_t v[SAMPLES];
};

// v[i] = 32767 * sin(2 pi i / 256), computed at compile time.
constexpr SineTable makeSine() {
    SineTable t{};
    for (unsigned i = 0; i < SAMPLES; i++) {
        double x = 2.0 * PI * i / SAMPLES;
        if (x > PI) x -= 2.0 * PI;
        double s = taylorSin(x) * 32767.0;
        t.v[i] = static_cast<int32_t>(s < 0 ? s - 0.5 : s + 0.5);
    }
    return t;
}

constexpr SineTable SINE = makeSine();

// Adds amplitude * sin(2 pi k n / 256 + phase) to the window, phase in
// quarter turns.
void addTone(int32_t* acc, unsigned bin, int32_t amplitude, unsigned quarterTurns = 0) {
    for (unsigned n = 0; n < SAMPLES; n++) {
        unsigned index = (bin * n + quarterTurns * (SAMPLES / 4)) % SAMPLES;
        acc[n] += SINE.v[index] * amplitude / 32767;
    }
}

struct Result {
    uint16_t bands[BANDS];
};

Result run(const int32_t* acc) {
    int16_t samples[SAMPLES];
    for (unsigned n = 0; n < SAMPLES; n++) samples[n] = acc[n];
    Result r;
    Spectrum::computeBands(std::span<const int16_t, SAMPLES>(samples), std::span<uint16_t, BANDS>(r.bands));
    return r;
}

unsigned bandOf(unsigned bin) {
    for (unsigned b = 0; b < BANDS; b++) {
        if (bin >= BAND_EDGES[b] && bin < BAND_EDGES[b + 1]) return b;
    }
    return BANDS;
}

bool within(uint16_t value, int32_t expected, int32_t percent) {
    int32_t slack = expected * percent / 100;
    return value >= expected - slack && value <= expected + slack;
}

// Leakage allowed into a band with no tone in it: 1% of full scale.
constexpr uint16_t FLOOR = 328;

}  // namespace

TEST_CASE("Spectrum band edges cover bins 1 to 127") {
    CHECK(BAND_EDGES[0] == 1);
    CHECK(BAND_EDGES[BANDS] == SAMPLES / 2);
    for (unsigned b = 0; b < BANDS; b++) CHECK(BAND_EDGES[b] < BAND_EDGES[b + 1]);
}

TEST_CASE("Spectrum of silence is zero") {
    int32_t acc[SAMPLES] = {};
    Result r = run(acc);
    for (unsigned b = 0; b < BANDS; b++) CHECK(r.bands[b] <= 2);
}

TEST_CASE("Spectrum ignores DC") {
    int32_t acc[SAMPLES];
    for (unsigned n = 0; n < SAMPLES; n++) acc[n] = 16384;
    Result r = run(acc);
    for (unsigned b = 0; b < BANDS; b++) CHECK(r.bands[b] <= FLOOR);
}

TEST_CASE("Spectrum puts a tone in its band and nowhere else") {
    constexpr int32_t A = 32000;
    // The first and the last bin of every band.
    for (unsigned b = 0; b < BANDS; b++) {
        unsigned bins[2] = {BAND_EDGES[b], unsigned(BAND_EDGES[b + 1] - 1)};
        for (unsigned bin : bins) {
            CAPTURE(b, bin);
            int32_t acc[SAMPLES] = {};
            addTone(acc, bin, A);
            Result r = run(acc);
            CHECK(within(r.bands[b], A, 3));
            for (unsigned other = 0; other < BANDS; other++) {
                if (other == b) continue;
                CAPTURE(other);
                CHECK(r.bands[other] <= FLOOR);
            }
        }
    }
}

TEST_CASE("Spectrum magnitude does not depend on phase") {
    constexpr int32_t A = 20000;
    for (unsigned quarter = 0; quarter < 4; quarter++) {
        CAPTURE(quarter);
        int32_t acc[SAMPLES] = {};
        addTone(acc, 37, A, quarter);
        Result r = run(acc);
        CHECK(within(r.bands[bandOf(37)], A, 3));
    }
}

TEST_CASE("Spectrum is linear in amplitude") {
    const int32_t amplitudes[] = {32000, 16000, 4000, 1000};
    for (int32_t a : amplitudes) {
        CAPTURE(a);
        int32_t acc[SAMPLES] = {};
        addTone(acc, 9, a);
        Result r = run(acc);
        // 3%, or 40 counts for the quiet ones, whichever is larger.
        int32_t slack = a * 3 / 100 < 40 ? 40 : a * 3 / 100;
        uint16_t v = r.bands[bandOf(9)];
        CHECK(v >= a - slack);
        CHECK(v <= a + slack);
    }
}

TEST_CASE("Spectrum separates two tones") {
    constexpr int32_t A = 15000;
    int32_t acc[SAMPLES] = {};
    addTone(acc, 3, A);
    addTone(acc, 60, A, 1);
    Result r = run(acc);
    CHECK(within(r.bands[bandOf(3)], A, 3));
    CHECK(within(r.bands[bandOf(60)], A, 3));
    for (unsigned b = 0; b < BANDS; b++) {
        if (b == bandOf(3) || b == bandOf(60)) continue;
        CAPTURE(b);
        CHECK(r.bands[b] <= FLOOR);
    }
}

TEST_CASE("Spectrum cost, printed for the record") {
    // Root counter 2 at system clock / 8. Not asserted: emulator timing is
    // not silicon timing.
    int32_t acc[SAMPLES] = {};
    addTone(acc, 5, 12000);
    addTone(acc, 41, 9000, 1);
    addTone(acc, 101, 6000, 2);
    int16_t samples[SAMPLES];
    for (unsigned n = 0; n < SAMPLES; n++) samples[n] = acc[n];
    uint16_t bands[BANDS];
    COUNTERS[2].mode = TM_CLK_DIV8;
    uint16_t start = COUNTERS[2].value;
    Spectrum::computeBands(std::span<const int16_t, SAMPLES>(samples), std::span<uint16_t, BANDS>(bands));
    uint16_t end = COUNTERS[2].value;
    ramsyscall_printf("computeBands: %u cycles\n", unsigned(uint16_t(end - start)) * 8);
    CHECK(true);
}
