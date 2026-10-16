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

#include "snitch_all.hpp"

using namespace psyqo;

namespace {

// Builds a TIM in place: the 8 byte file header, an optional CLUT section, then
// the pixel section. Returns the file size. Sections are filled with zeroes;
// these tests only look at the headers.
uint32_t buildTIM(uint8_t* out, uint32_t flags, unsigned clutW, unsigned clutH, unsigned pixW,
                  unsigned pixH) {
    uint32_t cursor = 0;
    auto put32 = [&](uint32_t v) {
        for (unsigned i = 0; i < 4; i++) out[cursor++] = uint8_t(v >> (i * 8));
    };
    auto section = [&](uint16_t x, uint16_t y, unsigned w, unsigned h) {
        put32(12 + w * h * 2);
        put32(x | (uint32_t(y) << 16));
        put32(w | (uint32_t(h) << 16));
        for (unsigned i = 0; i < w * h * 2; i++) out[cursor++] = 0;
    };
    put32(0x10);
    put32(flags);
    if (flags & 8) section(0, 480, clutW, clutH);
    section(640, 0, pixW, pixH);
    return cursor;
}

}  // namespace

TEST_CASE("TIM 8bpp with CLUT parses") {
    alignas(4) uint8_t buffer[64];
    uint32_t size = buildTIM(buffer, 0x09, 2, 1, 1, 1);
    TIM tim;
    REQUIRE(tim.parse(buffer, size));
    REQUIRE(tim.error() == TIM::Error::None);
    REQUIRE(tim.hasCLUT());
    REQUIRE(tim.type() == TIM::Type::Tex8Bits);
    REQUIRE(tim.clut().rect.pos.x == 0);
    REQUIRE(tim.clut().rect.pos.y == 480);
    REQUIRE(tim.clut().rect.size.w == 2);
    REQUIRE(tim.clut().rect.size.h == 1);
    REQUIRE(tim.pixels().rect.pos.x == 640);
    REQUIRE(tim.pixels().rect.size.w == 1);
    REQUIRE(tim.width() == 2);
    REQUIRE(tim.height() == 1);
    REQUIRE(tim.clut().aligned);
    REQUIRE(tim.pixels().aligned);
    REQUIRE(tim.pixels().data == reinterpret_cast<const uint16_t*>(buffer + 8 + 12 + 4 + 12));
}

TEST_CASE("TIM without CLUT parses") {
    alignas(4) uint8_t buffer[64];
    uint32_t size = buildTIM(buffer, 0x02, 0, 0, 4, 2);
    TIM tim;
    REQUIRE(tim.parse(buffer, size));
    REQUIRE(!tim.hasCLUT());
    REQUIRE(tim.type() == TIM::Type::Tex16Bits);
    REQUIRE(tim.width() == 4);
    REQUIRE(tim.height() == 2);
}

TEST_CASE("TIM 4bpp width is in texels") {
    alignas(4) uint8_t buffer[64];
    uint32_t size = buildTIM(buffer, 0x08, 16, 1, 3, 1);
    TIM tim;
    REQUIRE(tim.parse(buffer, size));
    REQUIRE(tim.type() == TIM::Type::Tex4Bits);
    REQUIRE(tim.width() == 12);
}

TEST_CASE("TIM odd sized CLUT leaves the pixels unaligned") {
    // Three palette entries are six bytes, which puts the pixel data two bytes
    // off a word boundary. The file is still valid.
    alignas(4) uint8_t buffer[64];
    uint32_t size = buildTIM(buffer, 0x09, 3, 1, 1, 1);
    TIM tim;
    REQUIRE(tim.parse(buffer, size));
    REQUIRE(tim.clut().aligned);
    REQUIRE(!tim.pixels().aligned);
}

TEST_CASE("TIM section size disagreeing with its dimensions is rejected") {
    alignas(4) uint8_t buffer[64];
    uint32_t size = buildTIM(buffer, 0x09, 2, 1, 1, 1);
    buffer[8 + 8] = 99;  // CLUT width no longer matches the declared size
    TIM tim;
    REQUIRE(!tim.parse(buffer, size));
    REQUIRE(tim.error() == TIM::Error::BadSectionSize);
}

TEST_CASE("TIM section size check does not wrap") {
    // 0xffff by 0xffff halfwords is 0x1fffc0002 bytes, which wraps to
    // 0xfffc0002 in 32 bits. A declared size of that plus the header must not
    // be taken for a match.
    alignas(4) uint8_t buffer[64];
    uint32_t size = buildTIM(buffer, 0x02, 0, 0, 1, 1);
    uint32_t declared = 0xfffc0002 + 12;
    for (unsigned i = 0; i < 4; i++) buffer[8 + i] = uint8_t(declared >> (i * 8));
    buffer[8 + 8] = 0xff;
    buffer[8 + 9] = 0xff;
    buffer[8 + 10] = 0xff;
    buffer[8 + 11] = 0xff;
    TIM tim;
    REQUIRE(!tim.parse(buffer, size));
    REQUIRE(tim.error() == TIM::Error::BadSectionSize);
}

#ifdef DEBUG_TIM
TEST_CASE("TIM debug checks reject malformed files") {
    alignas(4) uint8_t buffer[64];
    TIM tim;

    uint32_t size = buildTIM(buffer, 0x09, 2, 1, 1, 1);
    buffer[0] = 0x11;
    REQUIRE(!tim.parse(buffer, size));
    REQUIRE(tim.error() == TIM::Error::BadMagic);

    size = buildTIM(buffer, 0x09, 2, 1, 1, 1);
    buffer[2] = 0x01;
    REQUIRE(!tim.parse(buffer, size));
    REQUIRE(tim.error() == TIM::Error::Compressed);

    size = buildTIM(buffer, 0x0f, 2, 1, 1, 1);
    REQUIRE(!tim.parse(buffer, size));
    REQUIRE(tim.error() == TIM::Error::BadType);

    REQUIRE(!tim.parse(buffer, 4));
    REQUIRE(tim.error() == TIM::Error::TooSmall);

    REQUIRE(!tim.parse(nullptr, size));
    REQUIRE(tim.error() == TIM::Error::TooSmall);

    size = buildTIM(buffer, 0x09, 2, 1, 1, 1);
    REQUIRE(!tim.parse(buffer, size - 1));
    REQUIRE(tim.error() == TIM::Error::Truncated);
}
#endif
