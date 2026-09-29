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

// OOP / psyqo rewrite of the OpenBIOS tiny shell (src/mips/shell). It renders
// the same intro: a cube that spins up out of a 2D square, a periodic convex
// hull "ping", colour moods driven by CD-ROM state, and a tracker soundtrack.
// When a bootable disc is found it plays the success/outro stings, fades to
// black, and then returns from main() so the BIOS can boot the disc.
//
// psyqo's Application::run() never returns (it is an infinite frame loop), so
// the "exit main when ready to boot" behaviour is implemented with the BIOS
// setjmp/longjmp syscalls: main() sets a jump buffer before run(), and the
// boot sequence longjmps back to it.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/psxlibc/setjmp.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/lines.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/quads.hh"
#include "psyqo/scene.hh"
#include "shello/cdcheck.hh"
#include "shello/fixedmath.hh"
#include "shello/hull.hh"
#include "shello/sound.hh"

using namespace shello;

namespace {

constexpr int WIDTH = 640;
constexpr int HEIGHT = 480;

// Raw 8.24 unit (the original's ONE == 16777216) used for the lerp bookkeeping
// that the shell keeps in plain integers.
constexpr int32_t ONE_I = 1 << 24;
constexpr unsigned DC_2PI = 2048;
constexpr unsigned DC_PI2 = 512;
constexpr unsigned DC_PI4 = 256;

constexpr Fixed fixedRaw(int32_t raw) { return Fixed(raw, Fixed::RAW); }

class Shell final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    // Tear down the live subsystems and longjmp back to main(), which then
    // returns so the BIOS can continue booting. This is the psyqo equivalent
    // of the original shell breaking out of its main loop.
    [[noreturn]] void requestBoot();

    psyqo::Trig<24> m_trig;
    Sound m_sound;
    CDChecker m_checker;
    unsigned m_fps = 60;
    struct JmpBuf m_exitJmpBuf;
};

class MainScene final : public psyqo::Scene {
    void start(StartReason reason) override;
    void frame() override;

    // --- model -------------------------------------------------------------
    // clang-format off
    //           x
    //     *------>
    //    /|
    //  z/ |      6--------7
    //  L  |y    /|       /|
    //     V    2--------3 |
    //          | |      | |
    //          | 5------|-4
    //          |/       |/
    //          1--------0
    // clang-format on
    static constexpr Vec3 c_modelVertices[8] = {
        {.x = 1.0, .y = 1.0, .z = 1.0},    {.x = -1.0, .y = 1.0, .z = 1.0},  {.x = -1.0, .y = -1.0, .z = 1.0},
        {.x = 1.0, .y = -1.0, .z = 1.0},   {.x = 1.0, .y = 1.0, .z = -1.0},  {.x = -1.0, .y = 1.0, .z = -1.0},
        {.x = -1.0, .y = -1.0, .z = -1.0}, {.x = 1.0, .y = -1.0, .z = -1.0},
    };
    static constexpr unsigned c_modelQuads[6][4] = {
        {0, 1, 2, 3}, {0, 4, 5, 1}, {0, 3, 7, 4}, {4, 7, 6, 5}, {2, 6, 7, 3}, {1, 5, 6, 2},
    };
    static constexpr Vec3 c_modelNormals[6] = {
        {.x = 0.0, .y = 0.0, .z = 1.0},  {.x = 0.0, .y = 1.0, .z = 0.0},  {.x = 1.0, .y = 0.0, .z = 0.0},
        {.x = 0.0, .y = 0.0, .z = -1.0}, {.x = 0.0, .y = -1.0, .z = 0.0}, {.x = -1.0, .y = 0.0, .z = 0.0},
    };

    // --- colours -----------------------------------------------------------
    static constexpr psyqo::Color c_black = {{.r = 0, .g = 0, .b = 0}};
    static constexpr psyqo::Color c_white = {{.r = 255, .g = 255, .b = 255}};
    static constexpr psyqo::Color c_bgIdle = {{.r = 0, .g = 64, .b = 91}};
    static constexpr psyqo::Color c_fgIdle = {{.r = 156, .g = 220, .b = 218}};
    static constexpr psyqo::Color c_bgError = {{.r = 60, .g = 18, .b = 0}};
    static constexpr psyqo::Color c_fgError = {{.r = 220, .g = 156, .b = 156}};
    static constexpr psyqo::Color c_bgSuccess = {{.r = 36, .g = 91, .b = 0}};
    static constexpr psyqo::Color c_fgSuccess = {{.r = 152, .g = 224, .b = 155}};

    // --- rotation speeds ---------------------------------------------------
    static constexpr Fixed c_xRotSpeedIdle = fixedRaw(3 * ONE_I / (2 * DC_2PI));
    static constexpr Fixed c_yRotSpeedIdle = fixedRaw(4 * ONE_I / (3 * DC_2PI));
    static constexpr Fixed c_zRotSpeedIdle = fixedRaw(ONE_I / DC_2PI);
    static constexpr Fixed c_xRotSpeedError = fixedRaw(0);
    static constexpr Fixed c_yRotSpeedError = fixedRaw(ONE_I / (DC_2PI * 3));
    static constexpr Fixed c_zRotSpeedError = fixedRaw(0);

    // --- live state --------------------------------------------------------
    // v: first 8 entries are the projected cube vertices, last 8 the hull work
    // buffer (convexHull writes its result there).
    Vec2 m_v[16];
    Fixed m_n[6];

    int m_hull = 0;
    int m_hullFrame = 0;

    unsigned m_frameCounter = 0;
    int m_phase = 0;

    int32_t m_quarterSecLerpSpeed = 0;

    psyqo::Color m_bg;
    psyqo::Color m_fg;
    psyqo::Color m_blackCol;

    Fixed m_xRotSpeed = c_xRotSpeedIdle;
    Fixed m_yRotSpeed = c_yRotSpeedIdle;
    Fixed m_zRotSpeed = c_zRotSpeedIdle;
    Fixed m_xRotAccel = fixedRaw(0);
    Fixed m_yRotAccel = fixedRaw(0);
    Fixed m_zRotAccel = fixedRaw(0);
    Fixed m_xRot = fixedRaw(0);
    Fixed m_yRot = fixedRaw(0);
    Fixed m_zRot = fixedRaw(ONE_I / 8);
    Fixed m_scale = fixedRaw(ONE_I);

    int m_scheduleBoot = 0;
    int m_bootFrames = 0;
    bool m_wasError = false;
    bool m_wasSuccess = false;

    // --- lerp engine -------------------------------------------------------
    // The original keyframes colours and rotation speeds with a tiny lerp
    // engine. Two flavours are used: colour (C) and 8.24-domain scalar (D).
    enum LerpId {
        LERP_TO_IDLE,
        LERP_TO_SUCCESS,
        LERP_TO_ERROR,
        LERP_TO_OUTRO,
        LERP_TO_BLACK,
    };
    enum LerpType { LERPD, LERPC };
    struct Lerp {
        Fixed ds, dd;
        Fixed *dr;
        psyqo::Color cs, cd;
        psyqo::Color *cr;
        int32_t p;
        int32_t speed;
        LerpType type;
    };
    Lerp m_lerps[7];

    // --- render chain ------------------------------------------------------
    // Everything is drawn through a DMA chain (ordering table), never via
    // immediate sendPrimitive: mixing immediate polygons with the frame-flip
    // DMA chain desyncs the GPU FIFO. Double-buffered by parity because the
    // previous frame's chain may still be transferring.
    static constexpr unsigned OT_SIZE = 32;
    psyqo::OrderingTable<OT_SIZE> m_ots[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Quad> m_faceFrags[2][6];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Line> m_lineFrags[2][8];

    void initLerps();
    void startLerp(LerpId id);
    void applyLerps();
    void calculateFrame();
    void facesSort(unsigned *faces, int count);
    void render();
};

Shell g_shell;
MainScene g_mainScene;

// Project an 8.24 screen-space point to integer GPU coordinates. The shell
// scales by 128 (>>17 of an 8.24 unit) and centres the 640x480 frame. The
// aspect correction below works out to identity at 640x480 but is kept to
// mirror the original arithmetic exactly.
psyqo::Vertex project(const Vec2 &v) {
    int32_t x = v.x.raw() >> 17;
    int32_t y = v.y.raw() >> 17;
    y = y * HEIGHT * 4 / (WIDTH * 3);
    psyqo::Vertex r;
    r.x = int16_t(x + WIDTH / 2);
    r.y = int16_t(y + HEIGHT / 2);
    return r;
}

}  // namespace

void Shell::prepare() {
    // Region detection mirrors the BIOS: 'E' in the kernel header marks PAL.
    bool isPAL = (*((char *)0xbfc7ff52) == 'E');
    m_fps = isPAL ? 50 : 60;

    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W640)
        .set(isPAL ? psyqo::GPU::VideoMode::PAL : psyqo::GPU::VideoMode::NTSC)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::INTERLACED);
    gpu().initialize(config);

    m_checker.prepare(gpu());
    m_sound.init(gpu());
}

void Shell::createScene() {
    pushScene(&g_mainScene);
    m_checker.start();
}

void Shell::requestBoot() {
    m_sound.uninit(gpu());
    syscall_longjmp(&m_exitJmpBuf, 1);
}

void MainScene::start(StartReason reason) {
    m_quarterSecLerpSpeed = 4 * ONE_I / g_shell.m_fps;
    m_bg = m_fg = m_blackCol = c_black;
    initLerps();
    startLerp(LERP_TO_IDLE);
}

void MainScene::initLerps() {
    for (auto &l : m_lerps) {
        l = Lerp{};
        l.type = LERPC;
    }
    m_lerps[0].type = LERPC;
    m_lerps[0].cr = &m_bg;
    m_lerps[1].type = LERPC;
    m_lerps[1].cr = &m_fg;
    m_lerps[2].type = LERPC;
    m_lerps[2].cr = &m_blackCol;
    m_lerps[3].type = LERPD;
    m_lerps[3].dr = &m_xRotSpeed;
    m_lerps[4].type = LERPD;
    m_lerps[4].dr = &m_yRotSpeed;
    m_lerps[5].type = LERPD;
    m_lerps[5].dr = &m_zRotSpeed;
    m_lerps[6].type = LERPD;
    m_lerps[6].dr = &m_scale;
    m_lerps[6].ds = fixedRaw(ONE_I);
    m_lerps[6].dd = fixedRaw(ONE_I >> 3);
}

void MainScene::startLerp(LerpId lerpID) {
    m_lerps[0].p = m_lerps[1].p = m_lerps[2].p = m_lerps[3].p = m_lerps[4].p = m_lerps[5].p = 0;
    m_lerps[0].speed = m_lerps[1].speed = m_lerps[3].speed = m_lerps[4].speed = m_lerps[5].speed =
        m_quarterSecLerpSpeed;
    m_lerps[0].cs = *m_lerps[0].cr;
    m_lerps[1].cs = *m_lerps[1].cr;
    m_lerps[2].cs = *m_lerps[2].cr;
    m_lerps[3].ds = *m_lerps[3].dr;
    m_lerps[4].ds = *m_lerps[4].dr;
    m_lerps[5].ds = *m_lerps[5].dr;
    switch (lerpID) {
        case LERP_TO_IDLE:
            m_lerps[0].cd = c_bgIdle;
            m_lerps[1].cd = c_fgIdle;
            m_lerps[3].dd = c_xRotSpeedIdle;
            m_lerps[4].dd = c_yRotSpeedIdle;
            m_lerps[5].dd = c_zRotSpeedIdle;
            break;
        case LERP_TO_SUCCESS:
            m_lerps[0].cd = c_bgSuccess;
            m_lerps[1].cd = c_fgSuccess;
            m_lerps[3].dd = c_xRotSpeedIdle;
            m_lerps[4].dd = c_yRotSpeedIdle;
            m_lerps[5].dd = c_zRotSpeedIdle;
            break;
        case LERP_TO_ERROR:
            m_lerps[0].cd = c_bgError;
            m_lerps[1].cd = c_fgError;
            m_lerps[3].dd = c_xRotSpeedError;
            m_lerps[4].dd = c_yRotSpeedError;
            m_lerps[5].dd = c_zRotSpeedError;
            break;
        case LERP_TO_OUTRO:
            m_lerps[0].cd = c_white;
            m_lerps[1].cd = c_white;
            m_lerps[2].cd = c_white;
            m_lerps[0].speed = m_lerps[1].speed = m_lerps[2].speed = m_lerps[6].speed = m_quarterSecLerpSpeed >> 2;
            m_lerps[3].speed = m_lerps[4].speed = m_lerps[5].speed = 0;
            break;
        case LERP_TO_BLACK:
            m_lerps[0].cd = c_black;
            m_lerps[1].cd = c_black;
            m_lerps[2].cd = c_black;
            m_lerps[0].speed = m_lerps[1].speed = m_lerps[2].speed = m_quarterSecLerpSpeed >> 2;
            break;
    }
}

void MainScene::applyLerps() {
    for (auto &l : m_lerps) {
        if (l.speed == 0) continue;
        int32_t p = l.p;
        if (p >= ONE_I) l.speed = 0;
        switch (l.type) {
            case LERPD:
                *l.dr = lerpD(l.ds, l.dd, fixedRaw(p));
                break;
            case LERPC:
                *l.cr = lerpC(l.cs, l.cd, p >> 16);
                break;
        }
        p += l.speed;
        if (p > ONE_I) p = ONE_I;
        l.p = p;
    }
}

void MainScene::calculateFrame() {
    unsigned counter = m_frameCounter++;
    unsigned fps = g_shell.m_fps;
    if (counter == fps) m_phase = 1;
    int phase = m_phase;
    if ((counter + 4 * fps) % (5 * fps) == 1) {
        m_hullFrame = 0;
        m_hull = convexHull(m_v, 8);
        g_shell.m_sound.schedulePing();
    }
    Matrix3D transform;
    if (phase == 0) {
        uint32_t angle = DC_2PI - lerpU(0, DC_PI2 + DC_PI4, counter * 256 / fps);
        Fixed scale = lerpD(Fixed(1.58), Fixed(1.0), fixedRaw(int32_t(counter * ONE_I / fps)));
        generateRotationMatrix3D(&transform, dcToAngle(angle), Axis::Z, g_shell.m_trig);
        scaleMatrix3D(&transform, scale);
    } else {
        generateRotationMatrix3D(&transform, phaseToAngle(m_zRot), Axis::Z, g_shell.m_trig);
        Matrix3D rot;
        generateRotationMatrix3D(&rot, phaseToAngle(m_xRot), Axis::X, g_shell.m_trig);
        multiplyMatrix3D(&transform, &rot, &transform);
        generateRotationMatrix3D(&rot, phaseToAngle(m_yRot), Axis::Y, g_shell.m_trig);
        multiplyMatrix3D(&transform, &rot, &transform);
        scaleMatrix3D(&transform, m_scale);
        m_xRot += m_xRotSpeed;
        m_yRot += m_yRotSpeed;
        m_zRot += m_zRotSpeed;
        m_xRotSpeed += m_xRotAccel;
        m_yRotSpeed += m_yRotAccel;
        m_zRotSpeed += m_zRotAccel;
        Fixed one(1.0);
        Fixed zero(0.0);
        while (m_xRot >= one) m_xRot -= one;
        while (m_yRot >= one) m_yRot -= one;
        while (m_zRot >= one) m_zRot -= one;
        while (m_xRot < zero) m_xRot += one;
        while (m_yRot < zero) m_yRot += one;
        while (m_zRot < zero) m_zRot += one;
    }

    Fixed F = Fixed(12.0);
    Fixed one(1.0);
    for (unsigned i = 0; i < 8; i++) {
        Vec3 out;
        matrixVertexMul3D(&transform, &c_modelVertices[i], &out);
        if (phase == 0) {
            m_v[i].x = out.x;
            m_v[i].y = out.y;
        } else {
            Fixed d = F / (F + one - out.z);
            m_v[i].x = out.x * d;
            m_v[i].y = out.y * d;
        }
    }
    if (phase == 0) {
        m_n[0] = one;
        for (unsigned i = 1; i < 6; i++) {
            m_n[i] = -one;
        }
    } else {
        for (unsigned i = 0; i < 6; i++) {
            m_n[i] = matrixVertexMul3Dz(&transform, &c_modelNormals[i]);
        }
    }
}

void MainScene::facesSort(unsigned *faces, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (m_n[faces[j]] > m_n[faces[j + 1]]) {
                unsigned t = faces[j];
                faces[j] = faces[j + 1];
                faces[j + 1] = t;
            }
        }
    }
}

void MainScene::render() {
    unsigned fps = g_shell.m_fps;
    int parity = g_shell.gpu().getParity();
    auto &ot = m_ots[parity];
    auto &clear = m_clear[parity];

    // Clear is the first link of the chain; geometry is inserted into the
    // ordering table and chained after it. Higher OT z draws first.
    g_shell.gpu().getNextClear(clear.primitive, m_bg);
    g_shell.gpu().chain(clear);

    int hull = m_hull;
    if (hull) {
        unsigned counter = m_hullFrame++;
        unsigned p = counter * 256 * 3 / (2 * fps);
        psyqo::Color c = lerpC(m_fg, m_bg, p);
        Fixed s = lerpD(Fixed(1.0), Fixed(1.75), fixedRaw(int32_t(counter * ONE_I * 3 / (2 * fps))));
        Matrix3D m;
        m.vs[0].x = s;
        m.vs[0].y = fixedRaw(0);
        m.vs[1].x = fixedRaw(0);
        m.vs[1].y = s;
        psyqo::Vertex ve[8];
        for (int i = 0; i < hull; i++) {
            Vec2 p2 = m_v[i + 8];
            matrixVertexMul2D(&m, &p2);
            ve[i] = project(p2);
        }
        // Closed loop of line segments (the original streamed one polyline; the
        // rendered result is identical). Inserted at a high z so it draws behind
        // the cube faces.
        for (int i = 0; i < hull; i++) {
            auto &lf = m_lineFrags[parity][i];
            lf.primitive.setColor(c);
            lf.primitive.pointA = ve[i];
            lf.primitive.pointB = ve[(i + 1) % hull];
            ot.insert(lf, 25 - i);
        }
        if (counter >= 40) m_hull = 0;
    }

    unsigned faces[6];
    unsigned count = 0;
    Fixed zero(0.0);
    for (unsigned i = 0; i < 6; i++) {
        if (m_n[i] <= zero) continue;
        faces[count++] = i;
    }
    facesSort(faces, count);
    // faces[] is sorted back-to-front. Higher z draws first, so the backmost
    // face takes the highest z among the faces (all below the hull's range).
    for (unsigned i = 0; i < count; i++) {
        unsigned f = faces[i];
        unsigned p = m_n[f].raw() >> 16;
        psyqo::Color c = lerpC(m_blackCol, m_fg, p);
        auto &qf = m_faceFrags[parity][i];
        qf.primitive.setColor(c);
        qf.primitive.setPointA(project(m_v[c_modelQuads[f][0]]));
        qf.primitive.setPointB(project(m_v[c_modelQuads[f][1]]));
        qf.primitive.setPointC(project(m_v[c_modelQuads[f][3]]));
        qf.primitive.setPointD(project(m_v[c_modelQuads[f][2]]));
        qf.primitive.setOpaque();
        ot.insert(qf, 10 - i);
    }

    g_shell.gpu().chain(ot);
}

void MainScene::frame() {
    unsigned fps = g_shell.m_fps;

    // Boot sequence: success sting + spin-up, then outro, then fade to black,
    // then hand control back to the BIOS by longjmping out of run().
    if (m_scheduleBoot && m_phase != 0) {
        int bootFrames = m_bootFrames++;
        if (bootFrames == 0) {
            startLerp(LERP_TO_SUCCESS);
            m_xRotAccel = m_yRotAccel = m_zRotAccel = fixedRaw(2503);
        } else if (bootFrames == (int)fps) {
            g_shell.m_sound.scheduleOutro();
            startLerp(LERP_TO_OUTRO);
        } else if (bootFrames == (int)(fps * 2)) {
            startLerp(LERP_TO_BLACK);
        } else if (bootFrames == (int)(fps * 4)) {
            g_shell.requestBoot();  // does not return
        }
    }

    applyLerps();
    calculateFrame();

    bool isError = g_shell.m_checker.isError();
    bool isSuccess = g_shell.m_checker.isSuccess();
    if (isError && !m_wasError) {
        m_wasError = true;
        g_shell.m_sound.scheduleError();
        startLerp(LERP_TO_ERROR);
        pcsx_message("Invalid disc inserted, or no disc.\nUse File->Open ISO to insert a valid PlayStation disc.");
    } else if (isSuccess && !m_wasSuccess) {
        m_wasSuccess = true;
        if (!g_shell.m_checker.isAudio()) {
            m_scheduleBoot = 1;
        } else {
            // todo: play audio cd
        }
    } else if (!isError && m_wasError) {
        m_wasError = false;
        g_shell.m_sound.scheduleIdle();
        startLerp(LERP_TO_IDLE);
    }

    render();
}

int main() {
    if (syscall_setjmp(&g_shell.m_exitJmpBuf) == 0) {
        return g_shell.run();  // runs the frame loop; only escapes via longjmp
    }
    // Reached after requestBoot() longjmps here: the shell is done, return so
    // the BIOS continues booting the disc.
    return 0;
}
