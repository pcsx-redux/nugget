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

#include <span>

namespace Spectrum {

// One capture half: 256 mono samples at 44.1 kHz, about 5.8 ms of audio.
static constexpr unsigned SAMPLES = 256;
// One row of the waterfall.
static constexpr unsigned BANDS = 32;

// Band b covers the DFT bins k with BAND_EDGES[b] <= k < BAND_EDGES[b + 1].
// Bin k sits at k * 44100 / 256 Hz, about 172 Hz apart. The edges are close
// to geometric, one bin wide at the bottom, so the low bands are not crushed
// into the first column. DC (bin 0) and Nyquist (bin 128) belong to no band.
static constexpr uint8_t BAND_EDGES[BANDS + 1] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 15,  17,  19,  21,
    24, 27, 30, 34, 38, 42, 47, 53, 59, 66, 74, 83, 92, 103, 115, 128,
};

// Turns one window of time-domain samples into BANDS magnitudes, lowest
// frequency first. The waterfall only ever calls this.
//
// The window is the 256-point DFT X[k] of the samples as they are, with no
// windowing function. Bin k has magnitude 2 * |X[k]| / 256, so a sine of
// amplitude A centred on bin k reads A there. A band is the largest bin
// magnitude inside it, saturated to 32767.
void computeBands(std::span<const int16_t, SAMPLES> samples, std::span<uint16_t, BANDS> bands);

}  // namespace Spectrum
