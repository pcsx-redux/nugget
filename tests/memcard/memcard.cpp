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

// psyqo MemoryCard and MemoryCardFileSystem, against whatever card sits in
// MEMCARD_PORT (0 = slot 1, 1 = slot 2). THIS TEST ERASES THAT CARD. Before
// the first write it reads the card and refuses to go on if it holds any save
// this test did not write, so a card with real data on it survives a run on
// the wrong slot.
//
// Grading: every check prints PASS or FAIL, and the run ends on
// "memcard port N: all K checks passed" only when every check passed.
//
// The capacity probe writes a distinct tag to the last frame of each block and
// reads them all back. A 128 KiB card returns 16 distinct tags. A 64 KiB card
// that ignores the top sector address bit returns blocks 8..15's tags in
// blocks 0..7. Either is a pass: the probe reports what the card is. The fill
// test then writes one-block files until the filesystem stops, and checks the
// directory after each one. Whatever the card's size, the filesystem has to
// either keep every file intact or refuse the write; a write that reports OK
// and breaks the directory is a failure. A 64 KiB card holds 7 one-block
// files and refuses the 8th with OutOfSpace.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
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

class MemcardTest final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    MC m_card;
    FS m_fs{m_card};
};

class TestScene final : public psyqo::Scene {
    void frame() override;
    bool m_done = false;
};

MemcardTest app;
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

bool same(const uint8_t *a, const uint8_t *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

bool streq(const char *a, const char *b) {
    for (uint32_t i = 0;; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
}

bool startsWith(const uint8_t *s, const char *prefix) {
    for (uint32_t i = 0; prefix[i]; i++) {
        if (s[i] != static_cast<uint8_t>(prefix[i])) return false;
    }
    return true;
}

// Every file this test writes is named with this prefix.
constexpr const char kPrefix[] = "BIMEMTEST";
// writeFile stores the title as fullwidth Shift-JIS: 'M' 'T' is 82 6c 82 73.
constexpr const char kTitle[] = "MT MEMCARD TEST";
constexpr uint8_t kTitleSjis[] = {0x82, 0x6c, 0x82, 0x73};

alignas(4) uint8_t s_frame[128];
alignas(4) uint8_t s_frame2[128];
constexpr uint32_t kBig = 9000;
uint8_t s_payload[kBig];
uint8_t s_readback[kBig];

void fillFrame(uint8_t *frame, uint32_t seed) {
    for (uint32_t i = 0; i < 128; i++) frame[i] = static_cast<uint8_t>(seed * 37 + i * 11 + (seed >> 8));
}

void fillPayload(uint8_t *buf, uint32_t len, uint32_t seed) {
    uint32_t x = seed * 2654435761u + 1;
    for (uint32_t i = 0; i < len; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = static_cast<uint8_t>(x >> 16);
    }
}

void buildIcon(FS::Icon &icon, uint8_t frames) {
    __builtin_memset(&icon, 0, sizeof(icon));
    icon.frameCount = frames;
    for (unsigned i = 0; i < 16; i++) icon.clut[i] = static_cast<uint16_t>(i * 0x0842);
    for (unsigned f = 0; f < 3; f++) {
        for (unsigned i = 0; i < 128; i++) icon.pixels[f][i] = static_cast<uint8_t>(i + f * 0x31);
    }
}

// Reads the card before anything is written and decides whether it is ours to
// erase: blank (frame 0 all 00 or all FF), a Sony filesystem holding only
// files named with kPrefix, or a card a previous run left with one of its
// title frames over frame 0 (the half-size failure the fill test looks for).
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
    if (zeros || ones) {
        ramsyscall_printf("card: blank (frame 0 all %s)\n", zeros ? "00" : "ff");
        return true;
    }
    if (s_frame[0] == 'S' && s_frame[1] == 'C' && same(s_frame + 4, kTitleSjis, sizeof(kTitleSjis))) {
        ramsyscall_printf("card: frame 0 holds this test's title frame, from an earlier run\n");
        return true;
    }
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
            char name[21];
            for (unsigned i = 0; i < 20; i++) name[i] = static_cast<char>(s_frame[0x0a + i]);
            name[20] = 0;
            ramsyscall_printf("REFUSE: block %d holds a save this test did not write: %s\n", s, name);
            return false;
        }
    }
    ramsyscall_printf("card: Sony filesystem, no save but this test's\n");
    return true;
}

void testRange() {
    checkError(app.m_card.readSectorBlocking(kPort, 1024, s_frame), Error::BadSector, "read sector 1024");
    checkError(app.m_card.writeSectorBlocking(kPort, 1024, s_frame), Error::BadSector, "write sector 1024");
}

// Returns the card's size in blocks (16 or 8), or 0 if it answered neither way.
unsigned testCapacity() {
    bool ok = true;
    for (uint16_t b = 0; b < 16; b++) {
        fillFrame(s_frame, 0xca00 + b);
        Error e = app.m_card.writeSectorBlocking(kPort, b * 64 + 63, s_frame);
        if (e != Error::OK) {
            ramsyscall_printf("capacity: tag write to block %d failed: %s\n", b, MC::errorMessage(e));
            ok = false;
        }
    }
    int seen[16];
    for (uint16_t b = 0; b < 16; b++) {
        seen[b] = -1;
        Error e = app.m_card.readSectorBlocking(kPort, b * 64 + 63, s_frame);
        if (e != Error::OK) {
            ramsyscall_printf("capacity: tag read from block %d failed: %s\n", b, MC::errorMessage(e));
            ok = false;
            continue;
        }
        for (uint32_t t = 0; t < 16; t++) {
            fillFrame(s_frame2, 0xca00 + t);
            if (same(s_frame, s_frame2, 128)) seen[b] = t;
        }
    }
    bool full = ok, half = ok;
    for (int b = 0; b < 16; b++) {
        full = full && seen[b] == b;
        half = half && seen[b] == (b < 8 ? b + 8 : b);
    }
    ramsyscall_printf("capacity: tags read back:");
    for (int b = 0; b < 16; b++) ramsyscall_printf(" %d", seen[b]);
    ramsyscall_printf("\n");
    unsigned blocks = full ? 16 : half ? 8 : 0;
    if (blocks == 16) ramsyscall_printf("capacity: 16 blocks, 128 KiB\n");
    if (blocks == 8) ramsyscall_printf("capacity: 8 blocks, 64 KiB, block N+8 aliases block N\n");
#ifdef PCSX_TESTS
    check(blocks == 16, "capacity probe reads 16 distinct blocks");
#else
    check(blocks != 0, "capacity probe reads a 128 KiB or a 64 KiB card");
#endif
    return blocks;
}

void testRawFrames() {
    // All below sector 512, so a 64 KiB card holds them too. Frame 40 sits in
    // the directory block's unused area.
    static const uint16_t sectors[] = {40, 64, 127, 300, 511};
    bool ok = true;
    for (int pass = 0; pass < 2; pass++) {
        for (uint16_t s : sectors) {
            fillFrame(s_frame, s + pass * 0x500);
            if (pass) s_frame[0] = 0xff, s_frame[127] = 0x00;
            Error e = app.m_card.writeSectorBlocking(kPort, s, s_frame);
            if (e != Error::OK) {
                ramsyscall_printf("raw: write sector %d failed: %s\n", s, MC::errorMessage(e));
                ok = false;
            }
        }
        for (uint16_t s : sectors) {
            fillFrame(s_frame, s + pass * 0x500);
            if (pass) s_frame[0] = 0xff, s_frame[127] = 0x00;
            Error e = app.m_card.readSectorBlocking(kPort, s, s_frame2);
            if (e != Error::OK || !same(s_frame, s_frame2, 128)) {
                ramsyscall_printf("raw: sector %d pass %d: %s, %s\n", s, pass, MC::errorMessage(e),
                                  e == Error::OK ? "data differs" : "no data");
                ok = false;
            }
        }
    }
    check(ok, "raw frames: write, read back, overwrite, read back (5 sectors)");

    // The callback variant, through the same interrupt-driven transport.
    volatile bool done = false;
    Error result = Error::Timeout;
    __builtin_memset(s_frame2, 0, 128);
    app.m_card.readSector(kPort, 300, s_frame2, [&](Error e) {
        result = e;
        done = true;
    });
    while (!done) app.gpu().pumpCallbacks();
    fillFrame(s_frame, 300 + 0x500);
    s_frame[0] = 0xff, s_frame[127] = 0x00;
    check(result == Error::OK && same(s_frame, s_frame2, 128), "raw frames: callback read returns the frame");
}

void testFileSystem(unsigned capacity) {
    auto &gpu = app.gpu();
    auto &fs = app.m_fs;
    uint32_t freeBlocks = 0, len = 0, count = 0;
    bool exists = false;
    FS::Icon icon;
    buildIcon(icon, 2);

    if (!checkError(fs.formatBlocking(gpu, kPort), Error::OK, "fs: format")) return;
    checkError(fs.getCardStateBlocking(gpu, kPort), Error::OK, "fs: formatted card state");
    Error e = fs.getFreeBlockCountBlocking(gpu, kPort, &freeBlocks);
    // A 64 KiB card has 7 usable file blocks, a 128 KiB card 15.
    const uint32_t usable = capacity == 8 ? 7 : 15;
    check(e == Error::OK && freeBlocks == usable, "fs: every usable block free after format");

    fillPayload(s_payload, 200, 1);
    checkError(fs.writeFileBlocking(gpu, kPort, "BIMEMTEST-RW", kTitle, icon, s_payload, 200), Error::OK,
               "fs: write a one-block file");
    e = fs.fileExistsBlocking(gpu, kPort, "BIMEMTEST-RW", &exists);
    check(e == Error::OK && exists, "fs: file exists");
    __builtin_memset(s_readback, 0, 200);
    e = fs.readFileBlocking(gpu, kPort, "BIMEMTEST-RW", s_readback, 200, &len);
    check(e == Error::OK && len == 200 && same(s_payload, s_readback, 200), "fs: read back the payload");

    static FS::FileInfo info;
    e = fs.readFileInfoBlocking(gpu, kPort, "BIMEMTEST-RW", &info);
    check(e == Error::OK && same(info.title, kTitleSjis, sizeof(kTitleSjis)) && info.icon.frameCount == 2 &&
              same(reinterpret_cast<const uint8_t *>(info.icon.clut), reinterpret_cast<const uint8_t *>(icon.clut),
                   32) &&
              same(info.icon.pixels[0], icon.pixels[0], 128) && same(info.icon.pixels[1], icon.pixels[1], 128),
          "fs: title and icon read back");

    static FS::FileEntry entries[15];
    e = fs.listFilesBlocking(gpu, kPort, entries, 15, &count);
    check(e == Error::OK && count == 1 && streq(entries[0].name, "BIMEMTEST-RW") && entries[0].sizeInBlocks == 1 &&
              entries[0].firstBlock == 1,
          "fs: listing holds the one file, block 1");

    // 2 header frames + 9000 bytes = 2 blocks, 2 and 3: both under block 8.
    fillPayload(s_payload, kBig, 2);
    checkError(fs.writeFileBlocking(gpu, kPort, "BIMEMTEST-BIG", kTitle, icon, s_payload, kBig), Error::OK,
               "fs: write a two-block file");
    __builtin_memset(s_readback, 0, kBig);
    e = fs.readFileBlocking(gpu, kPort, "BIMEMTEST-BIG", s_readback, kBig, &len);
    check(e == Error::OK && len == kBig && same(s_payload, s_readback, kBig), "fs: read back 9000 bytes");
    e = fs.getFreeBlockCountBlocking(gpu, kPort, &freeBlocks);
    check(e == Error::OK && freeBlocks == usable - 3, "fs: 3 fewer free blocks with 1 + 2 in use");

    fillPayload(s_payload, 200, 3);
    checkError(fs.writeFileBlocking(gpu, kPort, "BIMEMTEST-RW", kTitle, icon, s_payload, 200), Error::OK,
               "fs: overwrite the one-block file");
    e = fs.readFileBlocking(gpu, kPort, "BIMEMTEST-RW", s_readback, 200, &len);
    check(e == Error::OK && len == 200 && same(s_payload, s_readback, 200), "fs: read back the new payload");
    e = fs.getFreeBlockCountBlocking(gpu, kPort, &freeBlocks);
    check(e == Error::OK && freeBlocks == usable - 3, "fs: overwrite left the free count alone");

    checkError(fs.writeFileBlocking(gpu, kPort, "BIMEMTEST-NAME-TOO-LONG", kTitle, icon, s_payload, 1),
               Error::NameTooLong, "fs: 23-character name");
    checkError(fs.writeFileBlocking(gpu, kPort, "BIMEMTEST-HUGE", kTitle, icon, s_payload, 15 * 8192),
               Error::FileTooLarge, "fs: 16-block file");

    checkError(fs.deleteFileBlocking(gpu, kPort, "BIMEMTEST-RW"), Error::OK, "fs: delete the one-block file");
    e = fs.fileExistsBlocking(gpu, kPort, "BIMEMTEST-RW", &exists);
    check(e == Error::OK && !exists, "fs: deleted file is gone");
    checkError(fs.readFileBlocking(gpu, kPort, "BIMEMTEST-RW", s_readback, 200, &len), Error::FileNotFound,
               "fs: read a deleted file");
    checkError(fs.deleteFileBlocking(gpu, kPort, "BIMEMTEST-RW"), Error::FileNotFound, "fs: delete it again");
    fillPayload(s_payload, kBig, 2);
    __builtin_memset(s_readback, 0, kBig);
    e = fs.readFileBlocking(gpu, kPort, "BIMEMTEST-BIG", s_readback, kBig, &len);
    check(e == Error::OK && len == kBig && same(s_payload, s_readback, kBig), "fs: other file still intact");
    checkError(fs.deleteFileBlocking(gpu, kPort, "BIMEMTEST-BIG"), Error::OK, "fs: delete the two-block file");
    e = fs.getFreeBlockCountBlocking(gpu, kPort, &freeBlocks);
    check(e == Error::OK && freeBlocks == usable, "fs: every usable block free after both deletes");

    // Make sector 512 repeat sector 0, the way a 64 KiB card reads, so the
    // filesystem's size probe is exercised on a 128 KiB card too. Formatting
    // again has to clear it.
    e = app.m_card.readSectorBlocking(kPort, 0, s_frame);
    if (e == Error::OK) e = app.m_card.writeSectorBlocking(kPort, 512, s_frame);
    if (e == Error::OK) e = fs.getFreeBlockCountBlocking(gpu, kPort, &freeBlocks);
    check(e == Error::OK && freeBlocks == 7, "fs: sector 512 repeating sector 0 reads as 7 usable blocks");
    unsigned aliasWritten = 0;
    Error aliasRefused = Error::OK;
    fillPayload(s_payload, 512, 0x200);
    for (unsigned n = 1; n <= 8; n++) {
        char name[21];
        __builtin_memcpy(name, "BIMEMTEST-A00", 14);
        name[12] = static_cast<char>('0' + n);
        aliasRefused = fs.writeFileBlocking(gpu, kPort, name, kTitle, icon, s_payload, 512);
        if (aliasRefused != Error::OK) break;
        aliasWritten = n;
    }
    check(aliasWritten == 7 && aliasRefused == Error::OutOfSpace,
          "fs: sector 512 repeating sector 0 takes 7 files, then out of space");
    checkError(fs.formatBlocking(gpu, kPort), Error::OK, "fs: format again");
    e = fs.getFreeBlockCountBlocking(gpu, kPort, &freeBlocks);
    check(e == Error::OK && freeBlocks == usable, "fs: format clears the copy of sector 0 at sector 512");
}

bool readsBack(char names[][21], unsigned k, unsigned after) {
    uint32_t len = 0;
    fillPayload(s_payload, 512, 0x100 + k);
    Error e = app.m_fs.readFileBlocking(app.gpu(), kPort, names[k - 1], s_readback, 512, &len);
    if (e == Error::OK && len == 512 && same(s_payload, s_readback, 512)) return true;
    ramsyscall_printf("fill: after file %d, file %d reads %s%s\n", after, k, MC::errorMessage(e),
                      e == Error::OK ? ", wrong data" : "");
    return false;
}

// Writes one-block files until the filesystem refuses, checking the listing
// and the new file after each write, and every file at the end.
void testFill(unsigned capacity) {
    auto &gpu = app.gpu();
    auto &fs = app.m_fs;
    FS::Icon icon;
    buildIcon(icon, 1);
    static char names[16][21];
    static FS::FileEntry entries[16];

    if (!checkError(fs.formatBlocking(gpu, kPort), Error::OK, "fill: format")) return;
    unsigned written = 0;
    bool intact = true;
    Error refused = Error::OK;
    for (unsigned n = 1; n <= 16 && intact; n++) {
        char *name = names[n - 1];
        __builtin_memcpy(name, "BIMEMTEST-F00", 14);
        name[11] = static_cast<char>('0' + n / 10);
        name[12] = static_cast<char>('0' + n % 10);
        fillPayload(s_payload, 512, 0x100 + n);
        Error e = fs.writeFileBlocking(gpu, kPort, name, kTitle, icon, s_payload, 512);
        if (e != Error::OK) {
            refused = e;
            ramsyscall_printf("fill: file %d refused: %s\n", n, MC::errorMessage(e));
            break;
        }
        written = n;
        // Every file written so far must still be listed and readable.
        uint32_t count = 0;
        e = fs.listFilesBlocking(gpu, kPort, entries, 16, &count);
        if (e != Error::OK || count != written) {
            ramsyscall_printf("fill: after file %d the listing reads %s, %d files\n", n, MC::errorMessage(e),
                              count);
            intact = false;
            break;
        }
        if (!readsBack(names, n, n)) intact = false;
    }
    // And all of them once more at the end.
    for (unsigned k = 1; k <= written && intact; k++) {
        if (!readsBack(names, k, written)) intact = false;
    }
    ramsyscall_printf("fill: %d one-block files written on a %d-block card\n", written, capacity);
    check(intact, "fill: every file written stays listed and readable");
    if (intact) {
        bool expected = written == 15 && refused == Error::OutOfSpace;
        if (capacity == 8) expected = written == 7 && refused == Error::OutOfSpace;
        check(expected, capacity == 8 ? "fill: half-size card, 7 files, then out of space"
                                      : "fill: 15 files, then out of space");
    }
}

}  // namespace

void MemcardTest::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
    m_card.prepare();
}

void MemcardTest::createScene() { pushScene(&scene); }

void TestScene::frame() {
    if (m_done) return;
    m_done = true;
    ramsyscall_printf("psyqo memory card, port %d (slot %d)\n", MEMCARD_PORT, MEMCARD_PORT + 1);

    if (check(app.m_card.probeBlocking(kPort) == Error::OK, "probe: a card answers") && cardIsOursToErase()) {
        testRange();
        unsigned capacity = testCapacity();
        testRawFrames();
        testFileSystem(capacity);
        testFill(capacity);
        checkError(app.m_fs.formatBlocking(app.gpu(), kPort), Error::OK, "cleanup: format");
    } else {
        s_failures++;
    }

    if (s_failures == 0) {
        ramsyscall_printf("memcard port %d: all %d checks passed\n", MEMCARD_PORT, s_checks);
    } else {
        ramsyscall_printf("memcard port %d: %d of %d checks FAILED\n", MEMCARD_PORT, s_failures, s_checks);
    }
    int code = s_failures == 0 ? 0 : 1;
    pcsx_exit(code);
    // On hardware: the break the farm reads back as the verdict, exit code in
    // $a0, as tests/support/runtime.c does.
    register int a0 asm("$4") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0) : "memory");
    while (1);
}

int main() { return app.run(); }
