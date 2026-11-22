/*

MIT License

Copyright (c) 2025 PCSX-Redux authors

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

#include "psyqo/application.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/primitives/common.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/quads.hh"
#include "psyqo/primitives/sprites.hh"
#include "psyqo/scene.hh"

namespace {

// This is the smallest example of the ordinary way to draw a textured sprite: the texture lives in
// the program as plain data, gets uploaded to VRAM together with its palette (the CLUT), and then
// is referenced by sprites and quads. See the `proctex` example for a texture that is computed
// instead of stored.

// The texture is 16x16 pixels at 4 bits per pixel (4bpp). That means each pixel is just an index,
// 0 to 15, into a palette of 16 colors. The palette is the CLUT (Color Look-Up Table). 4bpp is the
// smallest format the GPU supports, so it is cheap on VRAM and on the texture cache, and a small
// icon like this one only needs a handful of colors anyway.
//
// The VRAM is a grid of 16-bit halfwords, and the upload is expressed in those. Four 4-bit pixels
// make up one halfword, so a 16 pixel wide 4bpp texture is only 4 halfwords wide in VRAM. That is
// why the upload region below says `w = 4`, and not 16. The first pixel is in the lowest nibble of
// each halfword. The art is drawn in the comments; '.' is index 0.
// The DMA wants its source 4-byte aligned, hence the `alignas` on both arrays.
// clang-format off
alignas(4) static const uint16_t c_texture[16 * 4] = {
    0x0000, 0x1111, 0x1111, 0x0000,  // ....11111111....
    0x1100, 0x2221, 0x1222, 0x0011,  // ..111222222111..
    0x2110, 0x2222, 0x2222, 0x0112,  // .11222222222211.
    0x2210, 0x2222, 0x2222, 0x0122,  // .12222222222221.
    0x2221, 0x2233, 0x3322, 0x1222,  // 1222332222332221
    0x2221, 0x2243, 0x4322, 0x1222,  // 1222342222342221
    0x2221, 0x2233, 0x3322, 0x1222,  // 1222332222332221
    0x2221, 0x2222, 0x2222, 0x1222,  // 1222222222222221
    0x2221, 0x2222, 0x2222, 0x1222,  // 1222222222222221
    0x2521, 0x2222, 0x2222, 0x1252,  // 1252222222222521
    0x5221, 0x2222, 0x2222, 0x1225,  // 1225222222225221
    0x2210, 0x5555, 0x5555, 0x0122,  // .12255555555221.
    0x2110, 0x5552, 0x2555, 0x0112,  // .11225555552211.
    0x1100, 0x2221, 0x1222, 0x0011,  // ..111222222111..
    0x0000, 0x1111, 0x1111, 0x0000,  // ....11111111....
    0x0000, 0x0000, 0x0000, 0x0000,  // ................
};
// clang-format on

// The CLUT: 16 colors, 15-bit BGR (0bSBBBBBGGGGGRRRRR, five bits per channel). Index 0 is 0x0000,
// which, for a texel, means "transparent": the GPU does not draw texels whose full 16-bit color is
// 0. That is why the black pupils use 0x0421 (almost black), and not 0x0000. Unused entries are 0.
alignas(4) static const uint16_t c_clut[16] = {
    0x0000,  // 0: transparent
    0x08cc,  // 1: dark brown outline
    0x139f,  // 2: yellow
    0x7fff,  // 3: white
    0x0421,  // 4: near black
    0x109c,  // 5: red
};

// Where things go in VRAM. The VRAM is 1024x512 halfwords, and the display is at the top-left, so
// x = 768, y = 0 is to the right of the framebuffers, and does not collide with them.
// Textures are addressed through "texture pages" (tpages), which are 64 halfwords wide, and 256
// lines tall; a tpage can only start on a multiple of 64 on X, and of 256 on Y. 768 / 64 = 12.
static constexpr uint16_t c_textureX = 768;
static constexpr uint16_t c_textureY = 0;
static constexpr uint8_t c_tpageX = c_textureX / 64;
static constexpr uint8_t c_tpageY = c_textureY / 256;
// A CLUT must start on a multiple of 16 halfwords on X, but can be at any Y. We put it right under
// the texture's tpage, in a region where nothing else lives.
static constexpr uint16_t c_clutX = 768;
static constexpr uint16_t c_clutY = 256;

// The U and V of a texel are relative to the tpage's top-left corner, in texels (not halfwords!),
// so for a 4bpp texture, a tpage is 256 texels wide. Since the texture is at the very start of
// its tpage, its U and V start at 0. If we had uploaded it at x = 770, then U would start at 8.
static constexpr uint8_t c_textureU = 0;
static constexpr uint8_t c_textureV = 0;

class Sprite final : public psyqo::Application {
    void prepare() override;
    void createScene() override;
};

class SpriteScene final : public psyqo::Scene {
    void start(Scene::StartReason reason) override;
    void frame() override;

    // Everything we draw is sent through a DMA chain, and is double-buffered: while the GPU is
    // drawing the chain of the previous frame, we build the next one in the other buffer.
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> m_tpage[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Sprite16x16> m_bouncing[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Sprite16x16> m_tinted[2][4];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad> m_quad[2];

    // The state of the bouncing sprite.
    int16_t m_x = 40;
    int16_t m_y = 30;
    int16_t m_dx = 2;
    int16_t m_dy = 1;
};

Sprite sprite;
SpriteScene spriteScene;

}  // namespace

void Sprite::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void Sprite::createScene() { pushScene(&spriteScene); }

void SpriteScene::start(Scene::StartReason reason) {
    // The texture and its CLUT are in RAM, as part of the program. We copy them to VRAM using the
    // DMA, through `uploadToVRAM`. This is a blocking call, so it's fine to do it once at startup.
    // The data must be an array of 16-bit values, with exactly as many of them as the region has
    // halfwords: 4 x 16 for the texture, 16 x 1 for the CLUT.
    psyqo::Rect region = {.pos = {{.x = c_textureX, .y = c_textureY}}, .size = {{.w = 4, .h = 16}}};
    gpu().uploadToVRAM(c_texture, region);
    region = {.pos = {{.x = c_clutX, .y = c_clutY}}, .size = {{.w = 16, .h = 1}}};
    gpu().uploadToVRAM(c_clut, region);

    // The GPU has a small texture cache, and it is not aware of VRAM writes. If it holds stale data
    // for these locations, it'd draw the old content. `uploadToVRAM` already flushes it, so this
    // is redundant here, but it is the thing to do after any other way of changing a texture or a
    // CLUT, such as the manual upload in `proctex`, so we show it.
    psyqo::Prim::FlushCache fc;
    gpu().sendPrimitive(fc);
}

void SpriteScene::frame() {
    const int parity = gpu().getParity();

    // Clearing with `gpu().clear()` would send the clear right away, and not as part of our chain,
    // so we use a chained clear instead, which keeps everything in the order that we queue it.
    auto& clear = m_clear[parity];
    gpu().getNextClear(clear.primitive, {{.r = 0x20, .g = 0x30, .b = 0x60}});
    gpu().chain(clear);

    // The tpage tells the GPU where the texture is, and in which format. It applies to all the
    // textured primitives drawn after it. Sprites have no field for it, which is why it's separate.
    // We have to enable drawing in the display area, or nothing will show up there.
    auto& tpage = m_tpage[parity];
    tpage.primitive.attr.setPageX(c_tpageX)
        .setPageY(c_tpageY)
        .set(psyqo::Prim::TPageAttr::Tex4Bits)
        .enableDisplayArea();
    gpu().chain(tpage);

    // The primitives hold the CLUT as a single 16-bit value, with X in units of 16 halfwords. The
    // `Vertex` version of `ClutIndex` does that conversion for us; the (x, y) one wants X already
    // divided by 16.
    const psyqo::PrimPieces::ClutIndex clutIndex = psyqo::Vertex{{.x = c_clutX, .y = c_clutY}};

    // The texture info is the same for all of our sprites.
    const psyqo::PrimPieces::TexInfo texInfo = {.u = c_textureU, .v = c_textureV, .clut = clutIndex};

    // Let's bounce a sprite on the edges of the screen. A 16x16 sprite is the smallest of the
    // fixed-size sprites; `Sprite` takes an arbitrary size, and is a word longer.
    m_x += m_dx;
    m_y += m_dy;
    if (m_x <= 0 || m_x >= 320 - 16) m_dx = -m_dx;
    if (m_y <= 0 || m_y >= 240 - 16) m_dy = -m_dy;
    // The command color is multiplied with the texel color, as `texel * color / 128`. So
    // 0x80, 0x80, 0x80 means "unmodulated", and the texture is drawn as it is stored.
    auto& bouncing = m_bouncing[parity];
    bouncing.primitive.setColor({{.r = 0x80, .g = 0x80, .b = 0x80}});
    bouncing.primitive.position = {{.x = m_x, .y = m_y}};
    bouncing.primitive.texInfo = texInfo;
    gpu().chain(bouncing);

    // Here are more copies of the same sprite, with other colors. Values under 0x80 darken the
    // channel, while values over it brighten it, up to saturation. The yellow of the face is
    // (31, 28, 4) so it has very little blue to start with, and only responds to the other
    // channels. The outline and the eyes are dark, so they can't change by much.
    static constexpr psyqo::Color c_tints[4] = {
        {{.r = 0x80, .g = 0x80, .b = 0x80}},  // unmodulated
        {{.r = 0xff, .g = 0x40, .b = 0x40}},  // red, doubled
        {{.r = 0x40, .g = 0xff, .b = 0x40}},  // green, doubled
        {{.r = 0x40, .g = 0x40, .b = 0xff}},  // blue, doubled
    };
    for (unsigned i = 0; i < 4; i++) {
        auto& tinted = m_tinted[parity][i];
        tinted.primitive.setColor(c_tints[i]);
        tinted.primitive.position = {{.x = int16_t(16 + i * 24), .y = 200}};
        tinted.primitive.texInfo = texInfo;
        gpu().chain(tinted);
    }

    // Finally, a textured quad, using the same texture scaled up to 64x64. Unlike sprites, a quad
    // can be any shape, and has U and V for each corner, and carries the CLUT and the tpage
    // within the primitive itself. The corners are in Z order: top-left, top-right, bottom-left,
    // bottom-right. The UVs are the texels of the corresponding corner, within the tpage. The GPU
    // does not draw a polygon's bottom and right edges, so the UVs run from 0 to 16, and the last
    // drawn pixel lands inside texel 15. The GPU does not filter, so the scaled up result looks
    // blocky.
    auto& quad = m_quad[parity];
    auto& q = quad.primitive;
    q.setColor({{.r = 0x80, .g = 0x80, .b = 0x80}});
    q.pointA = {{.x = 240, .y = 24}};
    q.pointB = {{.x = 240 + 64, .y = 24}};
    q.pointC = {{.x = 240, .y = 24 + 64}};
    q.pointD = {{.x = 240 + 64, .y = 24 + 64}};
    q.uvA = {.u = c_textureU, .v = c_textureV};
    q.uvB = {.u = c_textureU + 16, .v = c_textureV};
    q.uvC = {.u = c_textureU, .v = c_textureV + 16};
    q.uvD = {.u = c_textureU + 16, .v = c_textureV + 16};
    q.clutIndex = clutIndex;
    q.tpage.setPageX(c_tpageX).setPageY(c_tpageY).set(psyqo::Prim::TPageAttr::Tex4Bits).enableDisplayArea();
    gpu().chain(quad);
}

int main() { return sprite.run(); }
