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

#include <EASTL/fixed_string.h>

#include "common/syscalls/syscalls.h"
#include "psyqo/application.hh"
#include "psyqo/cdrom-device.hh"
#include "psyqo/coroutine.hh"
#include "psyqo/font.hh"
#include "psyqo/gpu.hh"
#include "psyqo/iso9660-parser.hh"
#include "psyqo/scene.hh"
#include "psyqo/xprintf.h"

namespace {

// Lists a folder of the disc and counts the files in it. Change kFolder to
// the folder to list; an empty string lists the root.
constexpr const char kFolder[] = "DATA";

class CDRomDir final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::Font<> m_font;
    psyqo::CDRomDevice m_cdrom;
    psyqo::ISO9660Parser m_isoParser = psyqo::ISO9660Parser(&m_cdrom);
    eastl::fixed_string<char, 64> m_text = "Reading CD-Rom...";
    psyqo::Coroutine<> m_coroutine;

    psyqo::Coroutine<> listFolder();
};

class CDRomDirScene final : public psyqo::Scene {
    void frame() override;
};

CDRomDir cdromDir;
CDRomDirScene cdromDirScene;

}  // namespace

psyqo::Coroutine<> CDRomDir::listFolder() {
    using namespace psyqo::timer_literals;
    while (!co_await m_cdrom.reset()) co_await gpu().delay(1_s);
    while (!co_await m_isoParser.initialize()) {
        m_text = "No ISO9660 disc, retrying...";
        co_await gpu().delay(1_s);
    }

    psyqo::ISO9660Parser::DirEntry folder;
    if (!co_await m_isoParser.getDirentry(kFolder, &folder)) {
        m_text = "Failed reading the disc";
        co_return;
    }
    if (folder.type != psyqo::ISO9660Parser::DirEntry::DIRECTORY) {
        m_text.sprintf("No folder %s on the disc", kFolder);
        co_return;
    }

    unsigned files = 0;
    bool success = co_await m_isoParser.walkDirectory(
        folder, [&files](eastl::string_view name, const psyqo::ISO9660Parser::DirEntry& entry) {
            if (entry.type == psyqo::ISO9660Parser::DirEntry::FILE) {
                ramsyscall_printf("%.*s: %u bytes at LBA %u\n", int(name.size()), name.data(), entry.size, entry.LBA);
                files++;
            }
            return false;
        });
    if (!success) {
        m_text = "Failed reading the disc";
        co_return;
    }
    ramsyscall_printf("%u files in /%s\n", files, kFolder);
    m_text.sprintf("%u files in /%s", files, kFolder);
}

void CDRomDir::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
    m_cdrom.prepare();
}

void CDRomDir::createScene() {
    m_font.uploadSystemFont(gpu());
    pushScene(&cdromDirScene);
    m_coroutine = listFolder();
    m_coroutine.resume();
}

void CDRomDirScene::frame() {
    cdromDir.gpu().clear({{.r = 0, .g = 64, .b = 91}});
    cdromDir.m_font.print(cdromDir.gpu(), cdromDir.m_text, {{.x = 4, .y = 32}}, {{.r = 255, .g = 255, .b = 255}});
}

int main() { return cdromDir.run(); }
