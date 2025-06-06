/*

MIT License

Copyright (c) 2022 PCSX-Redux authors

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

namespace psyqo {

/**
 * @brief The Vertex struct.
 *
 * @details Represents a vertex in the VRAM space. Its binary representation is meant
 * to be the same as the vertices that GPU commands await. It should fit in a single
 * register, and the MIPS ABI ought to pass it as argument or return value gracefully,
 * making it a nice general purpose utility.
 *
 * Accessors can be either {x, y}, {u, v}, {s, t}, or {w, h}, for readability purposes.
 *
 * Using C++ list-initializations, one can create a vertex directly using such syntax:
 *
 *    `Vertex{{.x = 18, .y = 42}};`
 */
union Vertex {
    struct {
        union {
            int16_t x, u, s, w;
        };
        union {
            int16_t y, v, t, h;
        };
    };
    uint32_t packed;
};
static_assert(sizeof(Vertex) == sizeof(uint32_t), "Vertex is not 32 bits");

/**
 * @brief The Rect struct.
 *
 * @details Represents a rectangle in the VRAM space. Its binary representation is meant
 * to be the same as the rectangles that GPU commands await. It should fit in two
 * registers, and the MIPS ABI ought to pass it as argument or return value gracefully,
 * making it a nice general purpose utility.
 *
 * Accessors can be either {a, b}, or {pos, size}, for readability purposes.
 *
 * Using C++ list-initializations, one can create a rectangle directly using such syntax:
 *
 *    `Rect{.a = {.x = 18, .y = 42}, .b = {}};`
 */
struct Rect {
    union {
        Vertex a, pos;
    };
    union {
        Vertex b, size;
    };
    bool isEmpty() { return (size.w == 0) && (size.h == 0); }
};
static_assert(sizeof(Rect) == sizeof(uint64_t), "Rect is not 64 bits");

/**
 * @brief The Color struct
 *
 * @details Represents a 24-bits color, that works properly in GPU binary primitives.
 */

union Color {
    struct {
        uint8_t r, g, b, user;
    };
    uint32_t packed;
};
static_assert(sizeof(Color) == sizeof(uint32_t), "Color is not 32 bits");

namespace Prim::TPageAttr {
enum SemiTrans { HalfBackAndHalfFront, FullBackAndFullFront, FullBackSubFullFront, FullBackAndQuarterFront };
enum ColorMode { Tex4Bits, Tex8Bits, Tex16Bits };
}  // namespace Prim::TPageAttr

namespace Prim {
enum class Transparency { Auto, Opaque, SemiTransparent };
}

namespace PrimPieces {

/**
 * @brief A primitive's CLUT command.
 *
 * @details This is a common building block piece used in primitives.
 * The binary representation is meant to be the same as the CLUT argument for
 * GPU commands. The constructor can take either a `Vertex` or the raw x and y
 * coordinates for the CLUT to use.
 *
 * Remember that CLUTs are aligned to 16 pixels, so the vertex coordinates are rounded
 * to the lowest multiple of 16 on the X axis.
 */
struct ClutIndex {
    ClutIndex() {}
    ClutIndex(Vertex v) : ClutIndex(v.x >> 4, v.y) {}
    ClutIndex(uint16_t x, uint16_t y) : index((y << 6) | x) {}

  private:
    uint16_t index = 0;
};
static_assert(sizeof(ClutIndex) == sizeof(uint16_t), "ClutIndex is not 16 bits");

/**
 * @brief A primitive's texture information.
 *
 * @details This is a common building block piece used in primitives.
 * The binary representation is meant to be the same as the texture argument for
 * GPU commands.
 */
struct TexInfo {
    uint8_t u;
    uint8_t v;
    ClutIndex clut;
};
static_assert(sizeof(TexInfo) == sizeof(uint32_t), "TexInfo is not 32 bits");

struct TPageAttr;
/**
 * @brief A primitive's tpage location.
 *
 * @details This represents the location of a texture page in the VRAM space, and is
 * used in the `TPageAttr` struct. This can be used to easily set the page location
 * in a primitive's attribute while keeping the other attributes intact.
 */
struct TPageLoc {
    TPageLoc& setPageX(uint8_t x) {
        info &= ~0x000f;
        x &= 0x000f;
        info |= x;
        return *this;
    }
    TPageLoc& setPageY(uint8_t y) {
        info &= ~0x0010;
        y &= 0x0001;
        info |= y << 4;
        return *this;
    }

  private:
    uint8_t info = 0;
    friend struct TPageAttr;
};
static_assert(sizeof(TPageLoc) == sizeof(uint8_t), "TPageLoc is not 8 bits");

/**
 * @brief A primitive's tpage attribute.
 *
 * @details This is a common building block piece used in primitives.
 * The binary representation is meant to be the same as the 16-bits tpage argument for
 * GPU commands. See the `TPage` struct for actual usage, or the various triangle and
 * quad primitives.
 *
 * When dealing with tpage attributes, it can be more efficient to create a new
 * `TPageAttr` object on the stack, setting its properties, and then assigning it
 * to the primitive's attribute, instead of directly manipulating the existing one,
 * as the compiler will better optimize the former sequence of operations.
 */
struct TPageAttr {
    TPageAttr() {}
    TPageAttr(TPageAttr&& other) : info(other.info) {}
    TPageAttr(const TPageAttr& other) : info(other.info) {}
    TPageAttr& operator=(TPageAttr&& other) {
        info = other.info;
        return *this;
    }
    TPageAttr& operator=(const TPageAttr& other) {
        info = other.info;
        return *this;
    }
    TPageAttr& copy(const TPageAttr& other) {
        info = other.info;
        return *this;
    }
    TPageAttr& setPageX(uint8_t x) {
        info &= ~0x000f;
        x &= 0x000f;
        info |= x;
        return *this;
    }
    TPageAttr& setPageY(uint8_t y) {
        info &= ~0x0010;
        y &= 0x0001;
        info |= y << 4;
        return *this;
    }
    TPageAttr& setPageLoc(TPageLoc loc) {
        info &= ~0x001f;
        info |= loc.info;
        return *this;
    }
    TPageAttr& set(Prim::TPageAttr::SemiTrans trans) {
        info &= ~0x0060;
        uint32_t t = static_cast<uint32_t>(trans);
        info |= t << 5;
        return *this;
    }
    TPageAttr& set(Prim::TPageAttr::ColorMode mode) {
        info &= ~0x0180;
        uint32_t m = static_cast<uint32_t>(mode);
        info |= m << 7;
        return *this;
    }
    TPageAttr& setDithering(bool dithering) {
        if (dithering) {
            info |= 0x0200;
        } else {
            info &= ~0x0200;
        }
        return *this;
    }
    TPageAttr& disableDisplayArea() {
        info &= ~0x0400;
        return *this;
    }
    TPageAttr& enableDisplayArea() {
        info |= 0x0400;
        return *this;
    }
    uint8_t getPageX() const { return info & 0x000f; }
    uint8_t getPageY() const { return (info >> 4) & 0x0001; }
    uint8_t getPageLoc() const { return info & 0x001f; }
    Prim::TPageAttr::SemiTrans getSemiTrans() const {
        return static_cast<Prim::TPageAttr::SemiTrans>((info >> 5) & 0x0003);
    }
    Prim::TPageAttr::ColorMode getColorMode() const {
        return static_cast<Prim::TPageAttr::ColorMode>((info >> 7) & 0x0003);
    }
    bool hasDithering() const { return (info & 0x0200) != 0; }
    bool isDisplayAreaEnabled() const { return (info & 0x0400) != 0; }

  private:
    uint16_t info = 0;
};
static_assert(sizeof(TPageAttr) == sizeof(uint16_t), "TPageAttr is not 16 bits");

/**
 * @brief A primitive's page info attribute.
 *
 * @details This is a common building block piece used in primitives.
 */
struct PageInfo {
    uint8_t u;
    uint8_t v;
    TPageAttr attr;
};
static_assert(sizeof(PageInfo) == sizeof(uint32_t), "PageInfo is not 32 bits");

/**
 * @brief A primitive's UV coordinates attribute.
 *
 * @details This is a common building block piece used in primitives.
 */
struct UVCoords {
    uint8_t u;
    uint8_t v;
};
static_assert(sizeof(UVCoords) == sizeof(uint16_t), "UVCoords is not 16 bits");

/**
 * @brief A primitive's UV coordinates attribute.
 *
 * @details This is a common building block piece used in primitives.
 */
struct UVCoordsPadded {
    uint8_t u;
    uint8_t v;
    uint16_t user;
};
static_assert(sizeof(UVCoordsPadded) == sizeof(uint32_t), "UVCoordsPadded is not 32 bits");

}  // namespace PrimPieces

}  // namespace psyqo
