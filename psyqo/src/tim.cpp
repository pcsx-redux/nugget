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

#include "psyqo/tim.hh"

#include "psyqo/primitives/control.hh"
#include "psyqo/primitives/misc.hh"

namespace {

// The file is little endian and its sections are not guaranteed to sit on any
// particular boundary, so everything is assembled a byte at a time. A plain
// 16 or 32 bit load would fault on the MIPS for exactly the malformed files
// this parser is meant to survive.
uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

constexpr uint32_t c_fileHeaderSize = 8;
constexpr uint32_t c_sectionHeaderSize = 12;

} // namespace

bool psyqo::TIM::parse(const uint8_t* data, uint32_t size) {
    m_error = Error::None;
    m_hasCLUT = false;
    m_pixels = Section{};
    m_clut = Section{};

#ifdef DEBUG_TIM
    if (!data || size < c_fileHeaderSize) {
        m_error = Error::TooSmall;
        return false;
    }
#endif

#ifdef DEBUG_TIM
    if (data[0] != 0x10 || data[1] != 0x00) {
        m_error = Error::BadMagic;
        return false;
    }
#endif

    // A non-zero reserved field is how a compressed TIM announces itself. This
    // is a real format rather than corruption, but the data behind the header
    // is not what the rest of this function assumes, so refuse it plainly
    // instead of parsing garbage.
#ifdef DEBUG_TIM
    if (data[2] != 0x00 || data[3] != 0x00) {
        m_error = Error::Compressed;
        return false;
    }
#endif

    uint32_t flags = readU32(data + 4);
    uint32_t type = flags & 7;
#ifdef DEBUG_TIM
    if (type > uint32_t(Type::Mixed)) {
        m_error = Error::BadType;
        return false;
    }
#endif
    m_type = Type(type);
    m_hasCLUT = (flags & 8) != 0;

    uint32_t cursor = c_fileHeaderSize;

    auto readSection = [&](Section& out) {
#ifdef DEBUG_TIM
        if (size < cursor || size - cursor < c_sectionHeaderSize) {
            m_error = Error::Truncated;
            return false;
        }
#endif
        uint32_t declared = readU32(data + cursor);
        uint32_t coord = readU32(data + cursor + 4);
        uint32_t dimensions = readU32(data + cursor + 8);

        uint32_t width = dimensions & 0xffff;
        uint32_t height = dimensions >> 16;

        // The one check that is not debug-only, and the reason is the failure
        // mode rather than distrust. Everything else guarded above fails
        // visibly: no picture, or an obviously wrong one, on the first run.
        // This catches a bad *width*, and the width is what the upload size is
        // computed from, so without it a malformed section does not draw wrong,
        // it transfers past the end of the buffer. That surfaces later,
        // somewhere unrelated, as memory corruption with nothing pointing back
        // here.
        //
        // Compared in halfwords: width * height always fits 32 bits, while
        // width * 2 * height can wrap and match a bogus declared size.
        uint32_t bytes = declared - c_sectionHeaderSize;
        if (declared < c_sectionHeaderSize || (bytes & 1) || (bytes >> 1) != width * height) {
            m_error = Error::BadSectionSize;
            return false;
        }
#ifdef DEBUG_TIM
        if (declared < c_sectionHeaderSize || size - cursor < declared) {
            m_error = Error::Truncated;
            return false;
        }
#endif

        const uint8_t* payload = data + cursor + c_sectionHeaderSize;
        out.data = reinterpret_cast<const uint16_t*>(payload);
        out.rect.pos.x = int16_t(coord & 0xffff);
        out.rect.pos.y = int16_t(coord >> 16);
        out.rect.size.w = int16_t(width);
        out.rect.size.h = int16_t(height);
        out.aligned = (reinterpret_cast<uintptr_t>(payload) & 3) == 0;

        cursor += declared;
        return true;
    };

    if (m_hasCLUT && !readSection(m_clut)) return false;
    if (!readSection(m_pixels)) return false;

    return true;
}

unsigned psyqo::TIM::width() const {
    unsigned units = m_pixels.rect.size.w;
    switch (m_type) {
        case Type::Tex4Bits:
            return units * 4;
        case Type::Tex8Bits:
            return units * 2;
        case Type::Tex24Bits:
            // Three bytes per texel, so two texels occupy three VRAM units.
            return units * 2 / 3;
        case Type::Tex16Bits:
        case Type::Mixed:
        default:
            return units;
    }
}

namespace {

// DMA needs a four byte aligned source, and TIM sections are not reliably
// aligned: a CLUT with an odd number of entries leaves the pixel section two
// bytes off a word boundary, and
// shipped games do this. Rather than allocate a bounce buffer, feed the GPU
// through the FIFO by hand in that case, assembling each word bytewise. It is
// slower, and it only happens to sections DMA cannot take.
void uploadUnaligned(psyqo::GPU& gpu, const psyqo::TIM::Section& section, psyqo::Rect region) {
    psyqo::Prim::VRAMUpload upload;
    upload.region = region;
    gpu.sendPrimitive(upload);

    auto bytes = reinterpret_cast<const uint8_t*>(section.data);
    unsigned halfwords = unsigned(region.size.w) * unsigned(region.size.h);
    for (unsigned i = 0; i + 1 < halfwords; i += 2) {
        const uint8_t* p = bytes + i * 2;
        gpu.sendRaw(uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
                    (uint32_t(p[3]) << 24));
    }
    // A section with an odd halfword count still owes the GPU a full word.
    if (halfwords & 1) {
        const uint8_t* p = bytes + (halfwords - 1) * 2;
        gpu.sendRaw(uint32_t(p[0]) | (uint32_t(p[1]) << 8));
    }

    psyqo::Prim::FlushCache flush;
    gpu.sendPrimitive(flush);
}

void uploadSection(psyqo::GPU& gpu, const psyqo::TIM::Section& section, psyqo::Vertex position) {
    if (!section.data) return;
    psyqo::Rect region = {.pos = position, .size = section.rect.size};
    if (region.size.w <= 0 || region.size.h <= 0) return;
    // DMA moves whole words, so an odd number of halfwords goes through the
    // FIFO as well, even from an aligned address.
    bool wholeWords = ((unsigned(region.size.w) * unsigned(region.size.h)) & 1) == 0;
    if (section.aligned && wholeWords) {
        gpu.uploadToVRAM(section.data, region);
    } else {
        uploadUnaligned(gpu, section, region);
    }
}

} // namespace

void psyqo::TIM::upload(GPU& gpu) const {
    if (m_hasCLUT) uploadSection(gpu, m_clut, m_clut.rect.pos);
    uploadSection(gpu, m_pixels, m_pixels.rect.pos);
}

void psyqo::TIM::upload(GPU& gpu, Vertex texturePosition, Vertex clutPosition) const {
    if (m_hasCLUT) uploadSection(gpu, m_clut, clutPosition);
    uploadSection(gpu, m_pixels, texturePosition);
}

psyqo::PrimPieces::TPageAttr psyqo::TIM::tpage(Vertex texturePosition) const {
    PrimPieces::TPageAttr attr;
    attr.setPageX(uint8_t(texturePosition.x / 64)).setPageY(uint8_t(texturePosition.y / 256));
    switch (m_type) {
        case Type::Tex4Bits:
            attr.set(Prim::TPageAttr::Tex4Bits);
            break;
        case Type::Tex16Bits:
        case Type::Tex24Bits:
            // The GPU has no 24bpp texture mode; such a TIM is meant to be
            // viewed as a display area rather than sampled, so 16 bits is the
            // closest honest answer.
            attr.set(Prim::TPageAttr::Tex16Bits);
            break;
        case Type::Tex8Bits:
        case Type::Mixed:
        default:
            attr.set(Prim::TPageAttr::Tex8Bits);
            break;
    }
    return attr;
}

psyqo::PrimPieces::ClutIndex psyqo::TIM::clutIndex(Vertex clutPosition) const {
    return PrimPieces::ClutIndex(clutPosition);
}
