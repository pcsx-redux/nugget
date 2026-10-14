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

#include "psyqo/gpu.hh"
#include "psyqo/primitives/common.hh"

namespace psyqo {

/**
 * @brief A reader for Sony's TIM texture container.
 *
 * @details TIM is the devkit's texture format, and is what most PS1 games ship.
 * It is a thin wrapper: a short file header, then one optional CLUT section and
 * one pixel section, each carrying a destination in VRAM and its dimensions,
 * followed by data that is already in the layout the GPU wants. There is no
 * compression and no decoding, so loading one is a validation pass and two
 * transfers.
 *
 * The dimensions in a TIM section are in 16-bit VRAM units rather than in
 * texels, which happens to be exactly what `GPU::uploadToVRAM` wants, so the
 * section headers drop almost directly into a `Rect`.
 *
 * Validation is mostly a build-time concern. A TIM you produced yourself either
 * loads or visibly does not, and paying for those checks in every shipped binary
 * buys nothing, so define `DEBUG_TIM` to get them. The one exception is always
 * compiled in: a section whose declared size disagrees with its dimensions is
 * rejected unconditionally, because that is the only failure that is not
 * visible. It catches a bad width, the width is what the transfer length comes
 * from, and the result is a write past the end of the buffer rather than a
 * wrong picture.
 */
struct TIM {
    /**
     * @brief The pixel format of the data, as declared by the file.
     *
     * @details This is only a hint about how to *view* the pixels. The data is
     * copied into VRAM the same way regardless, so treat this as the source for
     * a texture page's colour mode rather than as something to branch on.
     * `Mixed` means the file holds several formats at once.
     */
    enum class Type : uint8_t {
        Tex4Bits = 0,
        Tex8Bits = 1,
        Tex16Bits = 2,
        Tex24Bits = 3,
        Mixed = 4,
    };

    /**
     * @brief Why a TIM was rejected.
     *
     * @details Explicit rather than a bool because the failures mean genuinely
     * different things: a compressed TIM is a valid file this parser refuses,
     * while a truncated one is a broken asset.
     *
     * The enum keeps its shape in every configuration on purpose. All but
     * `BadSectionSize` require `DEBUG_TIM` to be produced, but a type whose
     * definition changes between builds is how a header and a library end up
     * disagreeing, so the values stay put and simply go unused.
     */
    enum class Error : uint8_t {
        None = 0,
        TooSmall,      ///< The buffer cannot hold even a file header.
        BadMagic,      ///< The file does not start with 0x10, 0x00.
        Compressed,    ///< The reserved field is set, which marks a compressed TIM.
        BadType,       ///< The type field is outside the range the format defines.
        Truncated,     ///< A section claims to extend past the end of the buffer.
        BadSectionSize ///< A section's declared size disagrees with its dimensions.
    };

    /**
     * @brief One CLUT or pixel section.
     *
     * @details `rect` is in 16-bit VRAM units, matching both the file and
     * `GPU::uploadToVRAM`. For an 8bpp 256 texel wide image the width here is
     * 128, and for a 256 entry palette it is 256 by 1.
     */
    struct Section {
        const uint16_t* data = nullptr;
        Rect rect;
        bool aligned = false; ///< Whether `data` sits on a four byte boundary, as DMA requires.
    };

    /**
     * @brief Parses a TIM out of a buffer, without copying or taking ownership.
     *
     * @details The buffer has to outlive this object. Nothing is transferred to
     * VRAM here, so a caller can inspect the sections and decide where they
     * should land before committing to anything.
     *
     * @param data Start of the file.
     * @param size Bytes available. Checked against the section extents only
     * when `DEBUG_TIM` is defined.
     * @return Whether the file was accepted. On failure `error()` says why.
     */
    bool parse(const uint8_t* data, uint32_t size);

    /**
     * @brief Uploads the texture, and its palette if it has one, to the VRAM
     * coordinates the file itself asks for.
     *
     * @details Convenient for assets authored alongside the code that loads
     * them, and the wrong choice as soon as anything decides placement at
     * runtime, since the file wins. Prefer the overload taking explicit
     * positions when the application owns the VRAM layout.
     */
    void upload(GPU& gpu) const;

    /**
     * @brief Uploads to caller-chosen VRAM coordinates, ignoring the ones in
     * the file.
     *
     * @param texturePosition Where the pixels go. Has to sit on a texture page
     * boundary to be addressable: a multiple of 64 in x, of 256 in y.
     * @param clutPosition Where the palette goes, ignored when the file has no
     * CLUT. CLUTs are addressed in 16 pixel steps, so x should be a multiple
     * of 16.
     */
    void upload(GPU& gpu, Vertex texturePosition, Vertex clutPosition) const;

    /**
     * @brief Builds the texture page attribute describing where the pixels
     * landed, with the colour mode taken from the file's type.
     *
     * @param texturePosition The same position passed to `upload`, or
     * `pixels().rect.pos` when the file's own coordinates were used.
     */
    PrimPieces::TPageAttr tpage(Vertex texturePosition) const;

    /**
     * @brief Builds the CLUT index for a palette uploaded at the given VRAM
     * position. Meaningless when the file has no CLUT.
     */
    PrimPieces::ClutIndex clutIndex(Vertex clutPosition) const;

    bool hasCLUT() const { return m_hasCLUT; }
    Type type() const { return m_type; }
    Error error() const { return m_error; }

    const Section& pixels() const { return m_pixels; }
    const Section& clut() const { return m_clut; }

    /**
     * @brief The width of the image in texels rather than in VRAM units.
     *
     * @details The two differ everywhere except 16bpp, and confusing them is
     * the classic way to end up with a sheared texture.
     */
    unsigned width() const;
    unsigned height() const { return m_pixels.rect.size.h; }

  private:
    Section m_pixels;
    Section m_clut;
    Type m_type = Type::Tex4Bits;
    Error m_error = Error::None;
    bool m_hasCLUT = false;
};

} // namespace psyqo
