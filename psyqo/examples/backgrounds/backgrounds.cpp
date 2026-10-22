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

// Displays one of five textured background scenes, each a half skybox with a
// handful of props inside it. They are meant to be looked at rather than
// orbited, so the camera only pans and tilts a little, and the interesting
// motion comes from the directional light sweeping across the geometry.
//
// L1 and R1 cycle scenes, the d-pad moves the camera across and forwards, L2
// and R2 move it vertically, square and circle sweep the light by hand, cross
// toggles the automatic light sweep, and triangle cycles the culling mode.
//
// The meshes and textures are by MrQuetch (https://github.com/MrQuetch).

#include <stdint.h>

#include "mesh-renderer.hh"

extern "C" {
#include "common/syscalls/syscalls.h"
}
#include "psyqo/font.hh"
#include "psyqo/tim.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/scene.hh"
#include "psyqo/simplepad.hh"
#include "psyqo/soft-math.hh"
#include "psyqo/trigonometry.hh"

using namespace psyqo::fixed_point_literals;
using namespace psyqo::trig_literals;

// Meshes come from nugget.psxm, textures from nugget.bin2c. Both generate the
// headers that declare these, so nothing here has to know how they were built.
#include "bin2c_city_tim.h"
#include "bin2c_houses_tim.h"
#include "bin2c_leaves_tim.h"
#include "bin2c_palm_trees_tim.h"
#include "bin2c_volcano_tim.h"
#include "psxm_city_obj.h"
#include "psxm_houses_obj.h"
#include "psxm_leaves_obj.h"
#include "psxm_palm_trees_obj.h"
#include "psxm_volcano_obj.h"

namespace {

// Sized to the largest of the five scenes, with a little room to spare.
constexpr size_t MAX_VERTICES = 192;
constexpr size_t MAX_NORMALS = 160;
constexpr size_t MAX_TRIANGLES = 192;
constexpr size_t ORDERING_TABLE_SIZE = 2048;

constexpr unsigned SCENE_COUNT = 5;

struct SceneAsset {
    const unsigned char* mesh;
    const unsigned char* texture;
    uint32_t textureSize;
    const char* name;
};

const SceneAsset c_scenes[SCENE_COUNT] = {
    {psxm_city_obj, bin2c_city_tim, bin2c_city_tim_len, "City"},
    {psxm_houses_obj, bin2c_houses_tim, bin2c_houses_tim_len, "Houses"},
    {psxm_leaves_obj, bin2c_leaves_tim, bin2c_leaves_tim_len, "Leaves"},
    {psxm_palm_trees_obj, bin2c_palm_trees_tim, bin2c_palm_trees_tim_len, "Palm Trees"},
    {psxm_volcano_obj, bin2c_volcano_tim, bin2c_volcano_tim_len, "Volcano"},
};

// Only one scene is resident at a time, so a single texture page and a single
// palette row are all the VRAM this ever needs. x has to be a multiple of 64
// and y a multiple of 256 for a texture page; the palette goes in the strip
// between the two framebuffers.
constexpr psyqo::Vertex c_texturePosition = {{.x = 512, .y = 0}};
constexpr psyqo::Vertex c_clutPosition = {{.x = 0, .y = 240}};

}  // namespace

class Backgrounds final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::Font<> m_font;
    psyqo::Trig<> m_trig;
    psyqo::SimplePad m_input;
};

class BackgroundScene final : public psyqo::Scene {
    void start(StartReason reason) override;
    void frame() override;

    void loadScene(unsigned index);
    void handleInput();

    psxm::Model m_model;
    psyqo::TIM m_texture;
    psxm::Renderer<MAX_VERTICES, MAX_NORMALS, MAX_TRIANGLES> m_renderer;

    psyqo::OrderingTable<ORDERING_TABLE_SIZE> m_ots[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];

    unsigned m_sceneIndex = 0;
    unsigned m_lastDrawn = 0;
    unsigned m_frameCount = 0;

    // The camera sits inside the skybox, a little above the ground plane and
    // pulled back towards the open side of the dome.
    // Camera and field of view found by sweeping both against the geometry
    // rather than eyeballed: these scenes are built facing roughly +X, and at
    // this yaw the visible surfaces cover the whole 320x240 frame with no gaps
    // for all five of them. Pointing down +Z leaves a third of the screen
    // looking out of the open side of the dome.
    int32_t m_cameraX = 600;
    int32_t m_cameraY = -250;
    int32_t m_cameraZ = 300;
    psyqo::Angle m_yaw = 1.5_pi;
    psyqo::Angle m_pitch = 0.0_pi;

    psyqo::Angle m_lightAngle = 0.25_pi;
    bool m_animateLight = true;

    static constexpr psyqo::Color c_background = {{.r = 0, .g = 0, .b = 0}};
    // Texture modulation on the PS1 is neutral at 0x80, not 0xff, so the lit
    // and unlit ends have to straddle that value. Treating 0xff as "bright"
    // doubles the texel and blows the sky out to white.
    static constexpr psyqo::Color c_ambient = {{.r = 0x4c, .g = 0x4c, .b = 0x5c}};
    static constexpr psyqo::Color c_diffuse = {{.r = 0x98, .g = 0x94, .b = 0x88}};
};

static Backgrounds g_backgrounds;
static BackgroundScene g_backgroundScene;

void Backgrounds::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void Backgrounds::createScene() {
    m_font.uploadSystemFont(gpu());
    m_input.initialize();
    pushScene(&g_backgroundScene);
}

void BackgroundScene::loadScene(unsigned index) {
    m_sceneIndex = index % SCENE_COUNT;
    ramsyscall_printf("SCENE %u %s\n", m_sceneIndex, c_scenes[m_sceneIndex].name);
    const auto& scene = c_scenes[m_sceneIndex];
    m_model.parse(scene.mesh);
    m_texture.parse(scene.texture, scene.textureSize);
    // The TIM carries its own VRAM coordinates, and they are deliberately
    // ignored: the application owns the layout here. Uploading straight over
    // the previous scene's page is the whole point of a single slot, so
    // swapping costs one transfer and no bookkeeping.
    //
    // A switch happens mid-run, while the previous frame's DMA chain may still
    // be going out, and the GPU takes one transfer at a time. Wait for the
    // chain first.
    gpu().waitChainIdle();
    m_texture.upload(gpu(), c_texturePosition, c_clutPosition);
}

void BackgroundScene::start(StartReason reason) {
#ifndef BACKGROUNDS_START_SCENE
#define BACKGROUNDS_START_SCENE 0
#endif
    if (reason == StartReason::Create) loadScene(BACKGROUNDS_START_SCENE);
    m_renderer.setProjection(320, 240, 250);

    // Scene switching and the light toggle want edges rather than levels, so
    // they go through the event callback instead of being polled.
    g_backgrounds.m_input.setOnEvent([this](psyqo::SimplePad::Event event) {
        if (event.type != psyqo::SimplePad::Event::ButtonPressed) return;
        switch (event.button) {
            case psyqo::SimplePad::L1:
                loadScene(m_sceneIndex + SCENE_COUNT - 1);
                break;
            case psyqo::SimplePad::R1:
                loadScene(m_sceneIndex + 1);
                break;
            case psyqo::SimplePad::Cross:
                m_animateLight = !m_animateLight;
                break;
            case psyqo::SimplePad::Triangle: {
                using Culling = decltype(m_renderer)::Culling;
                auto next = m_renderer.culling();
                if (next == Culling::Normal) {
                    next = Culling::Inverted;
                } else if (next == Culling::Inverted) {
                    next = Culling::Disabled;
                } else {
                    next = Culling::Normal;
                }
                m_renderer.setCulling(next);
            } break;
            default:
                break;
        }
    });
}

void BackgroundScene::handleInput() {
    auto& pad = g_backgrounds.m_input;
    constexpr int32_t step = 8;

    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::Left)) m_cameraX -= step;
    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::Right)) m_cameraX += step;
    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::Up)) m_cameraZ += step;
    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::Down)) m_cameraZ -= step;
    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::L2)) m_cameraY += step;
    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::R2)) m_cameraY -= step;

    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::Square)) m_lightAngle -= 0.004_pi;
    if (pad.isButtonPressed(psyqo::SimplePad::Pad1, psyqo::SimplePad::Circle)) m_lightAngle += 0.004_pi;
}

void BackgroundScene::frame() {
#ifdef BACKGROUNDS_AUTOCYCLE
    // The scene switch re-uploads a TIM over a live one, which is the code path
    // a controller would drive. Without a pad there is no way to reach it, so a
    // headless run advances on a timer instead. Same call, same upload.
    if (++m_frameCount >= BACKGROUNDS_AUTOCYCLE) {
        m_frameCount = 0;
        loadScene(m_sceneIndex + 1);
    }
#endif
    handleInput();
    if (m_animateLight) m_lightAngle += 0.002_pi;

    unsigned parity = gpu().getParity();
    auto& ot = m_ots[parity];
    auto& clear = m_clear[parity];

    gpu().getNextClear(clear.primitive, c_background);
    gpu().chain(clear);

    // No model rotation: these scenes are built to be viewed from one side, so
    // the only orientation that moves is the camera's own.
    auto view = psyqo::SoftMath::generateRotationMatrix33(m_pitch, psyqo::SoftMath::Axis::X,
                                                          g_backgrounds.m_trig);
    auto yaw =
        psyqo::SoftMath::generateRotationMatrix33(m_yaw, psyqo::SoftMath::Axis::Y, g_backgrounds.m_trig);
    psyqo::SoftMath::multiplyMatrix33(view, yaw, &view);

    m_renderer.setCamera(view, m_cameraX, m_cameraY, m_cameraZ);

    // A directional light swinging around the vertical axis, tilted downwards
    // so it reads as a sun rather than a headlamp.
    psyqo::Vec3 light = {g_backgrounds.m_trig.cos(m_lightAngle), -0.55_fp,
                         g_backgrounds.m_trig.sin(m_lightAngle)};
    m_renderer.computeShading(m_model, light, c_ambient, c_diffuse);

    m_lastDrawn = m_renderer.render(m_model, ot, parity, m_texture.tpage(c_texturePosition),
                                    m_texture.clutIndex(c_clutPosition));

    gpu().chain(ot);

    auto stats = m_renderer.stats();
    constexpr psyqo::Color white = {{.r = 0xff, .g = 0xff, .b = 0xff}};
    g_backgrounds.m_font.chainprintf(gpu(), {{.x = 4, .y = 4}}, white, "%s %u/%u",
                                     c_scenes[m_sceneIndex].name, m_lastDrawn, m_model.triangleCount());
    g_backgrounds.m_font.chainprintf(gpu(), {{.x = 4, .y = 20}}, white, "near%u back%u rast%u depth%u",
                                     stats.rejectedNear, stats.rejectedBackface,
                                     stats.rejectedRasterizer, stats.rejectedDepth);
    g_backgrounds.m_font.chainprintf(gpu(), {{.x = 4, .y = 36}}, white, "cam %d %d %d cull%d", m_cameraX,
                                     m_cameraY, m_cameraZ, int(m_renderer.culling()));

    // Left and right on the d-pad now pan the yaw, since that is the axis the
    // scene actually reads along.
    
}

int main() { return g_backgrounds.run(); }
