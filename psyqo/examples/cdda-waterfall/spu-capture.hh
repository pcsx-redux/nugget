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

#include "spectrum.hh"

// Reads the SPU's CD audio capture buffers. The SPU writes every CD sample it
// receives into sound RAM: left at 0x000-0x3ff, right at 0x400-0x7ff, 512
// samples per channel, wrapping. SPUSTAT bit 11 says which half is being
// written, so whenever it flips, the other half is complete and stable for the
// next 256 samples (about 5.8 ms).
class SpuCapture {
  public:
    // Routes CD audio into the SPU at full volume. Call once the CD-ROM is idle.
    void prepare();
    // Checks bit 11 and, on a flip, copies the completed half into the window.
    // Returns true when the window was refreshed. Cheap when nothing changed, so
    // it can be polled from a fast timer: a half lasts 5.8 ms, and a missed flip
    // only means the window skips ahead to the next one.
    bool poll();
    // The latest complete 256-sample window, (L + R) / 2.
    std::span<const int16_t, Spectrum::SAMPLES> window() const { return m_window; }
    uint32_t halves() const { return m_halves; }

  private:
    int16_t m_window[Spectrum::SAMPLES];
    alignas(4) int16_t m_left[Spectrum::SAMPLES];
    alignas(4) int16_t m_right[Spectrum::SAMPLES];
    uint32_t m_halves = 0;
    bool m_lastHalf = false;
};
