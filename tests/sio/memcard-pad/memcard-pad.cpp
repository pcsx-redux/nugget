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

// AdvancedPad polling across a psyqo MemoryCardFileSystem write, against the
// card in MEMCARD_PORT (0 = slot 1, 1 = slot 2). THIS TEST ERASES THAT CARD,
// and, like tests/sio/memcard, refuses to touch a card holding a save it did
// not write.
//
// It writes a multi-block file with the asynchronous writeFile while the frame
// loop keeps running, and counts the pad polls that completed meanwhile
// (AdvancedPad::pollCount). The filesystem holds the SIO0 bus for the whole
// write, but the card is deselected between sector transfers, so the pad must
// keep polling: at least one poll for every two frames of the write. The file
// is then read back and compared byte for byte, so a poll that corrupted a
// transfer fails the test too.
//
// Grading: every check prints PASS or FAIL, and the run ends on
// "memcard-pad port N: all K checks passed" only when every check passed.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/advancedpad.hh"
#include "psyqo/application.hh"
#include "psyqo/gpu.hh"
#include "psyqo/memory-card-filesystem.hh"
#include "psyqo/memory-card.hh"
#include "psyqo/scene.hh"

#ifndef MEMCARD_PORT
#define MEMCARD_PORT 0
#endif

namespace {

using MC = psyqo::MemoryCard;
using FS = psyqo::MemoryCardFileSystem;
using Error = MC::Error;

constexpr MC::Port kPort = MEMCARD_PORT == 0 ? MC::Port::Port0 : MC::Port::Port1;

class MemcardPadTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    MC m_card;
    FS m_fs{m_card};
    psyqo::AdvancedPad m_pad;
};

class TestScene final : public psyqo::Scene {
    void frame() override;
    unsigned m_state = 0;
    unsigned m_frames = 0;
    uint32_t m_polls0 = 0;
    uint32_t m_writeFrames = 0;
    uint32_t m_writePolls = 0;
    bool m_writeDone = false;
    Error m_writeError = Error::OK;
};

MemcardPadTest app;
TestScene scene;

unsigned s_checks = 0;
unsigned s_failures = 0;

bool check(bool pass, const char *name) {
    s_checks++;
    if (!pass) s_failures++;
    ramsyscall_printf("%s %s\n", pass ? "PASS" : "FAIL", name);
    return pass;
}

bool checkError(Error got, Error want, const char *name) {
    s_checks++;
    bool pass = got == want;
    if (!pass) s_failures++;
    ramsyscall_printf("%s %s: %s", pass ? "PASS" : "FAIL", name, MC::errorMessage(got));
    if (!pass) ramsyscall_printf(" (want %s)", MC::errorMessage(want));
    ramsyscall_printf("\n");
    return pass;
}

// The file is named with the prefix tests/sio/memcard uses, so either test
// accepts a card the other one left behind.
constexpr const char kPrefix[] = "BIMEMTEST";
constexpr const char kName[] = "BIMEMTEST-PAD";
constexpr const char kTitle[] = "MT MEMCARD PAD TEST";
// Two blocks: the title and icon frames plus 9000 bytes do not fit in one.
constexpr uint32_t kLen = 9000;
uint8_t s_payload[kLen];
uint8_t s_readback[kLen];
alignas(4) uint8_t s_frame[128];
FS::Icon s_icon;

bool startsWith(const uint8_t *s, const char *prefix) {
    for (uint32_t i = 0; prefix[i]; i++) {
        if (s[i] != static_cast<uint8_t>(prefix[i])) return false;
    }
    return true;
}

// Blank, or a Sony filesystem whose only saves carry kPrefix. A test card a
// previous run of tests/sio/memcard left with a title frame over frame 0 is
// refused here; that test's own run formats it.
bool cardIsOursToErase() {
    Error e = app.m_card.readSectorBlocking(kPort, 0, s_frame);
    if (e != Error::OK) {
        ramsyscall_printf("REFUSE: frame 0 read failed: %s\n", MC::errorMessage(e));
        return false;
    }
    bool zeros = true, ones = true;
    for (unsigned i = 0; i < 128; i++) {
        zeros = zeros && s_frame[i] == 0x00;
        ones = ones && s_frame[i] == 0xff;
    }
    if (zeros || ones) return true;
    if (s_frame[0] != 'M' || s_frame[1] != 'C') {
        ramsyscall_printf("REFUSE: frame 0 is neither blank nor a Sony header\n");
        return false;
    }
    for (uint16_t s = 1; s <= 15; s++) {
        e = app.m_card.readSectorBlocking(kPort, s, s_frame);
        if (e != Error::OK) {
            ramsyscall_printf("REFUSE: directory frame %d read failed: %s\n", s, MC::errorMessage(e));
            return false;
        }
        if (s_frame[0] == 0x51 && !startsWith(s_frame + 0x0a, kPrefix)) {
            ramsyscall_printf("REFUSE: block %d holds a save this test did not write\n", s);
            return false;
        }
    }
    return true;
}

[[noreturn]] void finish() {
    if (s_failures == 0) {
        ramsyscall_printf("memcard-pad port %d: all %d checks passed\n", MEMCARD_PORT, s_checks);
    } else {
        ramsyscall_printf("memcard-pad port %d: %d of %d checks FAILED\n", MEMCARD_PORT, s_failures, s_checks);
    }
    int code = s_failures == 0 ? 0 : 1;
    pcsx_exit(code);
    // On hardware: the break the farm reads back as the verdict, exit code in
    // $a0, as tests/runtime/runtime.c does.
    register int a0 asm("$4") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0) : "memory");
    while (1);
}

}  // namespace

void MemcardPadTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
    m_pad.initialize();
    m_card.prepare();
}

void MemcardPadTest::createScene() { pushScene(&scene); }

void TestScene::frame() {
    auto &pad = app.m_pad;
    switch (m_state) {
        case 0: {
            ramsyscall_printf("psyqo memory card + pad, port %d (slot %d)\n", MEMCARD_PORT, MEMCARD_PORT + 1);
            if (!check(app.m_card.probeBlocking(kPort) == Error::OK, "probe: a card answers") ||
                !cardIsOursToErase()) {
                s_failures++;
                finish();
            }
            if (!checkError(app.m_fs.formatBlocking(app.gpu(), kPort), Error::OK, "format")) finish();
            for (uint32_t i = 0; i < kLen; i++) s_payload[i] = static_cast<uint8_t>(i * 7 + (i >> 8) * 13 + 0x5a);
            __builtin_memset(&s_icon, 0, sizeof(s_icon));
            s_icon.frameCount = 1;
            for (unsigned i = 0; i < 16; i++) s_icon.clut[i] = static_cast<uint16_t>(i * 0x0842);
            for (unsigned i = 0; i < 128; i++) s_icon.pixels[0][i] = static_cast<uint8_t>(i);
            m_frames = 0;
            m_polls0 = pad.pollCount();
            m_state = 1;
            break;
        }
        case 1:
            // Idle baseline: with no card operation the pad polls every frame.
            if (++m_frames < 60) break;
            {
                uint32_t polls = pad.pollCount() - m_polls0;
                ramsyscall_printf("baseline: %d polls in %d frames\n", polls, m_frames);
                check(polls + 2 >= m_frames, "baseline: the pad polls every frame with no card operation");
            }
            m_frames = 0;
            m_polls0 = pad.pollCount();
            m_state = 2;
            app.m_fs.writeFile(kPort, kName, kTitle, s_icon, s_payload, kLen, [this](Error e) {
                m_writeError = e;
                m_writeDone = true;
            });
            break;
        case 2:
            m_frames++;
            if (!m_writeDone) {
                if (m_frames > 60 * 60) {
                    check(false, "write: finished within 60 s");
                    finish();
                }
                break;
            }
            m_writeFrames = m_frames;
            m_writePolls = pad.pollCount() - m_polls0;
            m_state = 3;
            break;
        case 3: {
            checkError(m_writeError, Error::OK, "write: async writeFile");
            ramsyscall_printf("write: %d polls in %d frames\n", m_writePolls, m_writeFrames);
            check(m_writePolls * 2 >= m_writeFrames, "write: the pad polled at least every other frame");
            uint32_t got = 0;
            __builtin_memset(s_readback, 0, sizeof(s_readback));
            checkError(app.m_fs.readFileBlocking(app.gpu(), kPort, kName, s_readback, kLen, &got), Error::OK,
                       "read back: readFile");
            bool same = got == kLen;
            for (uint32_t i = 0; same && i < kLen; i++) same = s_readback[i] == s_payload[i];
            ramsyscall_printf("read back: %d bytes\n", got);
            check(same, "read back: every byte matches what was written");
            checkError(app.m_fs.formatBlocking(app.gpu(), kPort), Error::OK, "cleanup: format");
            finish();
        }
    }
}

int main() { return app.run(); }
