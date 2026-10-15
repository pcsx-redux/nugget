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

// Plays a movie off the disc in a loop, and prints how the playback went after
// each pass. Build with MOVIE=<iso9660 path>.

#include "common/syscalls/syscalls.h"
#include "psyqo-paths/str-player.hh"
#include "psyqo/application.hh"
#include "psyqo/cdrom-device.hh"
#include "psyqo/gpu.hh"
#include "psyqo/iso9660-parser.hh"
#include "psyqo/scene.hh"


namespace {

class StrPlayerExample final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::CDRomDevice m_cdrom;
    psyqo::ISO9660Parser m_isoParser = psyqo::ISO9660Parser(&m_cdrom);
    psyqo::paths::StrPlayer m_player = psyqo::paths::StrPlayer(m_cdrom, gpu());
    uint32_t m_lba = 0;
    uint32_t m_sectors = 0;
    bool m_resolved = false;
    bool m_ok = false;
    unsigned m_pass = 0;
};

class StrPlayerScene final : public psyqo::Scene {
    void frame() override;
};

StrPlayerExample s_app;
StrPlayerScene s_scene;

psyqo::paths::StrPlayer::Options options() {
    psyqo::paths::StrPlayer::Options o;
#ifdef STRPLAYER_RGB15
    o.rgb24 = false;
#endif
#ifdef STRPLAYER_ASAP
    o.present = psyqo::paths::StrPlayer::Present::ASAP;
#endif
#ifdef STRPLAYER_SLOTS
    o.frameSlots = STRPLAYER_SLOTS;
#endif
    return o;
}

void report(const psyqo::paths::StrPlayer::Stats &s, unsigned pass, int status) {
    ramsyscall_printf("STRP: pass %d status %d shown %d dropped %d rejected %d last %d\n", pass, status,
                      s.framesShown, s.framesDropped, s.framesRejected, s.lastFrameNumber);
    ramsyscall_printf("STRP: sectors video %d other %d errors %d incomplete %d ringfull %d\n", s.videoSectors,
                      s.otherSectors, s.readErrors, s.dropIncomplete, s.dropRingFull);
    ramsyscall_printf("STRP: vsyncs/frame 1:%d 2:%d 3:%d 4+:%d\n", s.vsyncHistogram[0], s.vsyncHistogram[1],
                      s.vsyncHistogram[2], s.vsyncHistogram[3]);
    ramsyscall_printf("STRP: switch line/32 %d %d %d %d %d %d %d %d %d %d\n", s.switchLineHistogram[0],
                      s.switchLineHistogram[1], s.switchLineHistogram[2], s.switchLineHistogram[3],
                      s.switchLineHistogram[4], s.switchLineHistogram[5], s.switchLineHistogram[6],
                      s.switchLineHistogram[7], s.switchLineHistogram[8], s.switchLineHistogram[9]);
    ramsyscall_printf("STRP: decode lines/64");
    for (unsigned i = 0; i < 12; i++) ramsyscall_printf(" %d", s.decodeLineHistogram[i]);
    ramsyscall_printf("\n");
    const uint8_t *o = s.firstOther;
    ramsyscall_printf("STRP: first other lba %d: %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x %02x %02x | %02x %02x %02x %02x\n",
                      s.firstOtherLba, o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9], o[10], o[11], o[12],
                      o[13], o[14], o[15]);
    ramsyscall_printf("STRP: reject why %08x mdec1 %08x chcr0 %08x chcr1 %08x cols %08x\n", s.rejectWhy, s.rejectMdec1,
                      s.rejectChcr0, s.rejectChcr1, s.rejectCols);
    ramsyscall_printf("STRP: reject madr0 %08x bcr0 %08x rl %08x inwords %d cmd %08x\n", s.rejectMadr0, s.rejectBcr0,
                      s.rejectRlStart, s.rejectInWords, s.rejectCommand);
}

}  // namespace

void StrPlayerExample::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
#ifdef STRPLAYER_RGB15
        .set(psyqo::GPU::ColorMode::C15BITS)
#else
        .set(psyqo::GPU::ColorMode::C24BITS)
#endif
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);
    m_cdrom.prepare();
}

void StrPlayerExample::createScene() {
    pushScene(&s_scene);
    m_isoParser.initialize([this](bool success) {
        if (!success) {
            ramsyscall_printf("STRP: iso9660 init failed\n");
            m_resolved = true;
            return;
        }
        psyqo::paths::resolveMovie(m_isoParser, STRPLAYER_MOVIE, [this](bool ok, uint32_t lba, uint32_t sectors) {
            ramsyscall_printf("STRP: %s ok %d lba %d sectors %d\n", STRPLAYER_MOVIE, ok, lba, sectors);
            m_lba = lba;
            m_sectors = sectors;
            m_ok = ok;
            m_resolved = true;
        });
    });
}

void StrPlayerScene::frame() {
    auto &app = s_app;
    if (!app.m_resolved || !app.m_ok) return;
    auto status = app.m_player.frame();
    if (status == psyqo::paths::StrPlayer::Status::PLAYING) {
        const auto &s = app.m_player.stats();
        if ((s.framesShown % 30) == 1) {
            ramsyscall_printf("STRP: progress shown %d dropped %d rejected %d video %d cdto %d %08x %08x %08x %08x\n",
                              s.framesShown, s.framesDropped, s.framesRejected, s.videoSectors, s.cdDmaTimeouts,
                              s.cdDmaChcr[0], s.cdDmaChcr[1], s.cdDmaChcr[2], s.cdDmaChcr[3]);
        }
        return;
    }
    if (status != psyqo::paths::StrPlayer::Status::IDLE) {
        report(app.m_player.stats(), app.m_pass, int(status));
        app.m_pass++;
    }
    if (!app.m_player.play(app.m_lba, app.m_sectors, options())) {
        ramsyscall_printf("STRP: play() could not allocate\n");
        app.m_ok = false;
    }
}

int main() { return s_app.run(); }
