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

// Running a hot routine with its stack in the scratchpad.
//
// The PS1 has no data cache. What it has instead is 1KB of scratchpad at
// 0x1f800000, which answers a load in one cycle, where a load from main RAM
// stalls the CPU for several. A routine that makes many calls spends a lot of
// its time on the stack: every call saves the registers it is going to use and
// the return address, and loads them all back before returning. Put that stack
// in the scratchpad and all of those accesses get cheap, without touching the
// routine itself.
//
// The tool for it is psyqo::ScratchpadStackful, a stackful coroutine whose
// stack is the top of the scratchpad. Nothing here yields: the coroutine is
// only a way to switch stacks around one call. Each frame, the scene creates
// it with the routine, resumes it, and the routine runs to the end and
// returns, all inside resume().
//
// The routine draws a Koch snowflake. Each edge is cut in three and the middle
// third is replaced by two sides of a triangle, recursively, so every call
// makes four calls and keeps its points alive across them. That is a lot of
// call overhead for not much arithmetic, which is the kind of routine this
// helps. The more arithmetic a routine does per call, the smaller the gain.
//
// The screen shows how long the routine took with the stack in main RAM and
// with the stack in the scratchpad, timed with root counter 2. Cross switches
// between the two. Until a button is pressed, the example switches on its own
// every few seconds, and prints each average over the serial port too.
//
// This only shows on a real console. PCSX-Redux does not charge the extra
// cycles a main RAM load costs, so in the emulator both numbers come out the
// same.
//
// The rules that come with the scratchpad stack are in the comment on
// ScratchpadStackful. The one this example has to mind is DMA: the routine
// writes its points to a buffer in main RAM, never to anything on its stack,
// because the GPU's DMA cannot read the scratchpad.

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/coroutine.hh"
#include "psyqo/fixed-point.hh"
#include "psyqo/font.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/primitives/lines.hh"
#include "psyqo/scene.hh"
#include "psyqo/simplepad.hh"
#include "psyqo/trigonometry.hh"

using namespace psyqo::fixed_point_literals;
using namespace psyqo::trig_literals;

namespace {

// Depth 4 is 3 * 4^4 = 768 segments, and 1023 calls of the recursive routine.
// Depth 5 would be more than 15ms with the stack in main RAM, past what a
// single read of the counter below can time.
constexpr unsigned DEPTH = 4;
constexpr unsigned SEGMENTS = 3 << (2 * DEPTH);

// The snowflake is drawn as polylines of 64 segments, the last point of one
// being the first point of the next.
constexpr unsigned SEGMENTS_PER_LINE = 64;
constexpr unsigned POLYLINES = SEGMENTS / SEGMENTS_PER_LINE;
static_assert(SEGMENTS % SEGMENTS_PER_LINE == 0);

// How much of the scratchpad the stack takes, from the top. The example
// measures how much of it the routine uses, and shows that on screen, which is
// how to size it for your own routine. The rest, at the bottom, stays free.
constexpr unsigned STACK_SIZE = 512;

// Frames per average, and how many averages before switching on our own.
constexpr unsigned FRAMES_PER_AVERAGE = 32;
constexpr unsigned AVERAGES_PER_SWITCH = 4;

// Points are in 1/16th of a pixel, so that the thirds of short edges do not
// all round to the same pixel.
struct Point {
    int32_t x, y;
};

class ScratchpadStackDemo final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::Font<> m_font;
    psyqo::SimplePad m_input;
    psyqo::Trig<> m_trig;
};

class SnowflakeScene final : public psyqo::Scene {
    void start(StartReason reason) override;
    void frame() override;

    // The routine being timed, and its recursion.
    void generate();
    void koch(Point a, Point b, unsigned depth);
    void paintStack();
    unsigned measureStack();
    void switchMode();

    psyqo::ScratchpadStackful<STACK_SIZE> m_coroutine;
    psyqo::Angle m_angle = 0.0_pi;
    psyqo::Vertex* m_out;
    bool m_onScratchpad = false;
    bool m_manual = false;

    unsigned m_frames = 0;
    unsigned m_averages = 0;
    uint32_t m_ticks = 0;
    uint32_t m_cycles[2] = {0, 0};
    unsigned m_stackUsed = 0;

    psyqo::Vertex m_points[SEGMENTS + 1];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::PolyLine<SEGMENTS_PER_LINE>> m_lines[2][POLYLINES];
};

ScratchpadStackDemo demo;
SnowflakeScene snowflakeScene;

}  // namespace

void ScratchpadStackDemo::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
}

void ScratchpadStackDemo::createScene() {
    m_font.uploadSystemFont(gpu());
    m_input.initialize();
    pushScene(&snowflakeScene);
}

void SnowflakeScene::start(StartReason reason) {
    // Counter 2 at the system clock divided by 8: one tick is 8 CPU cycles,
    // and the 16 bit counter wraps every 524288 cycles, about 15ms. The routine
    // has to stay under that for a single subtraction to time it.
    COUNTERS[2].mode = 0x0200;
    for (auto& lines : m_lines) {
        for (auto& line : lines) {
            line.primitive.setColor({{.r = 160, .g = 220, .b = 255}});
        }
    }
    demo.m_input.setOnEvent([this](psyqo::SimplePad::Event event) {
        if (event.type != psyqo::SimplePad::Event::ButtonPressed) return;
        m_manual = true;
        if (event.button == psyqo::SimplePad::Cross) switchMode();
    });
    ramsyscall_printf("scratchpad-stack: %u segments, %u byte stack\n", SEGMENTS, STACK_SIZE);
}

// Cuts the edge from a to b in three, and replaces the middle third with the
// two other sides of an equilateral triangle pointing outwards. The four edges
// that makes are cut again until depth reaches 0, where the edge is emitted.
void SnowflakeScene::koch(Point a, Point b, unsigned depth) {
    if (depth == 0) {
        *m_out++ = {{.x = int16_t(a.x >> 4), .y = int16_t(a.y >> 4)}};
        return;
    }
    int32_t dx = (b.x - a.x) / 3;
    int32_t dy = (b.y - a.y) / 3;
    Point p1 = {a.x + dx, a.y + dy};
    Point p3 = {b.x - dx, b.y - dy};
    // The middle third, turned 60 degrees: cos is 0.5 and sin is 0.866.
    constexpr psyqo::FixedPoint<> sin60 = 0.866_fp;
    Point p2 = {p1.x + dx / 2 + (sin60 * dy).integer(), p1.y + dy / 2 - (sin60 * dx).integer()};
    depth--;
    koch(a, p1, depth);
    koch(p1, p2, depth);
    koch(p2, p3, depth);
    koch(p3, b, depth);
}

void SnowflakeScene::generate() {
    auto& trig = demo.m_trig;
    Point corners[3];
    for (unsigned i = 0; i < 3; i++) {
        psyqo::Angle angle = m_angle + 0.6666667_pi * int32_t(i);
        // A radius of 100 pixels, in 1/16th of a pixel.
        corners[i].x = (160 << 4) + (trig.cos(angle) * 1600).integer();
        corners[i].y = (120 << 4) + (trig.sin(angle) * 1600).integer();
    }
    m_out = m_points;
    koch(corners[0], corners[1], DEPTH);
    koch(corners[1], corners[2], DEPTH);
    koch(corners[2], corners[0], DEPTH);
    *m_out = m_points[0];
}

// To see how much of the stack the routine needs, fill it with a pattern
// before the run and look for the deepest word that changed after it.
void SnowflakeScene::paintStack() {
    uint32_t* stack = reinterpret_cast<uint32_t*>(m_coroutine.c_stackBase);
    for (unsigned i = 0; i < STACK_SIZE / 4; i++) stack[i] = 0x5ca7c4ed;
}

unsigned SnowflakeScene::measureStack() {
    const uint32_t* stack = reinterpret_cast<const uint32_t*>(m_coroutine.c_stackBase);
    unsigned i = 0;
    while (i < STACK_SIZE / 4 && stack[i] == 0x5ca7c4ed) i++;
    return STACK_SIZE - i * 4;
}

void SnowflakeScene::switchMode() {
    m_onScratchpad = !m_onScratchpad;
    m_frames = 0;
    m_ticks = 0;
}

void SnowflakeScene::frame() {
    auto& gpu = demo.gpu();
    auto& font = demo.m_font;
    bool measureThisFrame = m_onScratchpad && (m_stackUsed == 0);
    if (measureThisFrame) paintStack();

    uint16_t start, end;
    if (m_onScratchpad) {
        start = COUNTERS[2].value;
        m_coroutine.initialize([this]() { generate(); });
        m_coroutine.resume();
        end = COUNTERS[2].value;
    } else {
        start = COUNTERS[2].value;
        generate();
        end = COUNTERS[2].value;
    }
    m_ticks += uint16_t(end - start);

    if (measureThisFrame) {
        m_stackUsed = measureStack();
        ramsyscall_printf("scratchpad-stack: %u bytes of stack used\n", m_stackUsed);
    }

    if (++m_frames == FRAMES_PER_AVERAGE) {
        uint32_t cycles = m_ticks * 8 / FRAMES_PER_AVERAGE;
        m_cycles[m_onScratchpad] = cycles;
        ramsyscall_printf("scratchpad-stack: %s %u cycles\n", m_onScratchpad ? "scratchpad" : "ram", cycles);
        m_frames = 0;
        m_ticks = 0;
        if (!m_manual && (++m_averages % AVERAGES_PER_SWITCH) == 0) switchMode();
    }
    m_angle += 0.002_pi;

    auto parity = gpu.getParity();
    psyqo::Color bg{{.r = 0, .g = 16, .b = 40}};
    gpu.getNextClear(m_clear[parity].primitive, bg);
    gpu.chain(m_clear[parity]);
    auto& lines = m_lines[parity];
    for (unsigned i = 0; i < POLYLINES; i++) {
        auto& points = lines[i].primitive.points;
        for (unsigned j = 0; j <= SEGMENTS_PER_LINE; j++) points[j] = m_points[i * SEGMENTS_PER_LINE + j];
        gpu.chain(lines[i]);
    }

    psyqo::Color white{{.r = 255, .g = 255, .b = 255}};
    psyqo::Color grey{{.r = 128, .g = 128, .b = 128}};
    font.chainprintf(gpu, {{.x = 4, .y = 4}}, white, "Stack: %s", m_onScratchpad ? "scratchpad" : "main RAM");
    font.chainprintf(gpu, {{.x = 4, .y = 20}}, m_onScratchpad ? grey : white, "main RAM:   %u cycles", m_cycles[0]);
    font.chainprintf(gpu, {{.x = 4, .y = 36}}, m_onScratchpad ? white : grey, "scratchpad: %u cycles", m_cycles[1]);
    if (m_cycles[0] && m_cycles[1] && (m_cycles[1] <= m_cycles[0])) {
        uint32_t saved = (m_cycles[0] - m_cycles[1]) * 1000 / m_cycles[0];
        font.chainprintf(gpu, {{.x = 4, .y = 52}}, white, "saved: %u.%u%%", saved / 10, saved % 10);
    } else if (m_cycles[0] && m_cycles[1]) {
        font.chainprintf(gpu, {{.x = 4, .y = 52}}, white, "no gain: is this an emulator?");
    }
    if (m_stackUsed) {
        font.chainprintf(gpu, {{.x = 4, .y = 212}}, grey, "stack used: %u of %u bytes", m_stackUsed, STACK_SIZE);
    }
    font.chainprintf(gpu, {{.x = 4, .y = 228}}, grey, "Cross: switch stacks");
}

int main() { return demo.run(); }
