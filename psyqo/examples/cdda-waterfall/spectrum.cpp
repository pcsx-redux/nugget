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

#include "spectrum.hh"

namespace {

uint32_t isqrt(uint32_t v) {
    uint32_t root = 0;
    uint32_t bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

}  // namespace

void Spectrum::computeBands(std::span<const int16_t, SAMPLES> samples, std::span<uint16_t, BANDS> bands) {
    // FFT goes here.
    //
    // This placeholder is not a spectrum: it splits the window into BANDS
    // consecutive slices of time and returns the RMS of each, so the field
    // moves with the music while the real transform is written. A
    // fixed-point FFT over the same 256 samples replaces this body, with
    // the same signature and the same output range.
    constexpr unsigned SLICE = SAMPLES / BANDS;
    for (unsigned band = 0; band < BANDS; band++) {
        uint32_t sum = 0;
        for (unsigned i = 0; i < SLICE; i++) {
            int32_t s = samples[band * SLICE + i];
            sum += static_cast<uint32_t>(s * s) / SLICE;
        }
        bands[band] = isqrt(sum);
    }
}
