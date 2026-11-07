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

#include "spu-capture.hh"

#include "common/hardware/cdrom.h"
#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/spu.h"
#include "psyqo/kernel.hh"

namespace {

constexpr uint32_t CAPTURE_LEFT = 0x000;
constexpr uint32_t CAPTURE_RIGHT = 0x400;
constexpr uint32_t HALF_BYTES = Spectrum::SAMPLES * sizeof(int16_t);

// Synchronous sound RAM -> main RAM DMA. psyqo::SPU only writes, so this is
// the read side, in the shape the SPU test suite uses on hardware.
void dmaRead(uint32_t spuAddress, void* dst, uint32_t bytes) {
    SPU_RAM_DTA = spuAddress / 8;
    for (unsigned i = 0; i < 0xf01 && SPU_RAM_DTA != spuAddress / 8; i++);
    SPU_CTRL = (SPU_CTRL & ~(0b11 << 4)) | (0b11 << 4);
    for (unsigned i = 0; i < 0xf01 && (SPU_STATUS & (0b11 << 4)) != (0b11 << 4); i++);
    // Sound RAM reads want a longer access time on the SPU bus than writes.
    uint32_t sbus = SBUS_DEV4_CTRL;
    SBUS_DEV4_CTRL = (sbus & 0xf0ffffff) | 0x02000000;

    DPCR |= 1 << 19;
    DMA_CTRL[DMA_SPU].MADR = reinterpret_cast<uint32_t>(dst) & 0x1fffffff;
    DMA_CTRL[DMA_SPU].BCR = ((bytes / 64) << 16) | 16;
    DMA_CTRL[DMA_SPU].CHCR = 0x01000200;
    while (DMA_CTRL[DMA_SPU].CHCR & 0x01000000);

    SBUS_DEV4_CTRL = sbus;
    SPU_CTRL &= ~(0b11 << 4);
    for (unsigned i = 0; i < 0xf01 && (SPU_STATUS & (0b11 << 4)) != 0; i++);
}

}  // namespace

void SpuCapture::prepare() {
    // CD-ROM decoder mixer: left to left and right to right at full scale,
    // no crossfeed, then latch the new values.
    CDROM_REG0 = 2;
    CDROM_REG2 = 0x80;
    CDROM_REG3 = 0x00;
    CDROM_REG0 = 3;
    CDROM_REG1 = 0x80;
    CDROM_REG2 = 0x00;
    CDROM_REG3 = 0x20;
    CDROM_REG0 = 0;
    // SPU side: CD input on, at full volume, so it is also audible. The
    // capture itself is written whatever these say.
    SPU_VOL_CD_LEFT = 0x3fff;
    SPU_VOL_CD_RIGHT = 0x3fff;
    SPU_CTRL |= 1;
    m_lastHalf = SPU_STATUS & 0x0800;
}

bool SpuCapture::poll() {
    bool half = SPU_STATUS & 0x0800;
    if (half == m_lastHalf) return false;
    m_lastHalf = half;
    // Bit 11 set: the SPU is writing the second half, so the first is done.
    uint32_t offset = half ? 0 : HALF_BYTES;
    dmaRead(CAPTURE_LEFT + offset, m_left, HALF_BYTES);
    dmaRead(CAPTURE_RIGHT + offset, m_right, HALF_BYTES);
    for (unsigned i = 0; i < Spectrum::SAMPLES; i++) {
        m_window[i] = (static_cast<int32_t>(m_left[i]) + m_right[i]) >> 1;
    }
    m_halves++;
    return true;
}
