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

#include "psyqo-paths/str-player.hh"

#include <EASTL/atomic.h>

extern "C" {
#include "bsdec/bsdec.h"
}
#include "common/hardware/counters.h"
#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/spu.h"
#include "psyqo-paths/archive-manager.hh"
#include "psyqo/hardware/cdrom.hh"
#include "psyqo/hardware/gpu.hh"
#include "psyqo/hardware/sbus.hh"
#include "psyqo/kernel.hh"
#include "psyqo/msf.hh"

namespace {

constexpr uint32_t c_chunkPayload = 2016;
constexpr uint32_t c_sectorHeader = 12 + 32;  // MSF + mode, subheader, STR chunk header
constexpr uint32_t c_dmaSpin = 2000000;

enum : uint8_t { SLOT_FREE, SLOT_FILLING, SLOT_READY };

volatile uint32_t &MDEC0 = *reinterpret_cast<volatile uint32_t *>(0x1f801820);
volatile uint32_t &MDEC1 = *reinterpret_cast<volatile uint32_t *>(0x1f801824);

// Sony's standard quantization table, in the zigzag order MDEC(2) consumes it,
// which is the order retail discs carry it in. Luma and chroma use the same one.
constexpr uint8_t c_quant[64] = {
    2,  16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27, 27, 27, 26, 26, 26, 26,
    27, 27, 27, 29, 29, 29, 34, 34, 34, 29, 29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38,
    37, 35, 35, 34, 35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83,
};

// The standard IDCT matrix for MDEC(3).
constexpr int16_t c_scale[64] = {
    0x5a82,  0x5a82,  0x5a82,  0x5a82, 0x5a82, 0x5a82,  0x5a82,  0x5a82,  0x7d8a,  0x6a6d,  0x471c,  0x18f8,  -0x18f9,
    -0x471d, -0x6a6e, -0x7d8b, 0x7641, 0x30fb, -0x30fc, -0x7642, -0x7642, -0x30fc, 0x30fb,  0x7641,  0x6a6d,  -0x18f9,
    -0x7d8b, -0x471d, 0x471c,  0x7d8a, 0x18f8, -0x6a6e, 0x5a82,  -0x5a83, -0x5a83, 0x5a82,  0x5a82,  -0x5a83, -0x5a83,
    0x5a82,  0x471c,  -0x7d8b, 0x18f8, 0x6a6d, -0x6a6e, -0x18f9, 0x7d8a,  -0x471d, 0x30fb,  -0x7642, 0x7641,  -0x30fc,
    -0x30fc, 0x7641,  -0x7642, 0x30fb, 0x18f8, -0x471d, 0x6a6d,  -0x7d8b, 0x7d8a,  -0x6a6e, 0x471c,  -0x18f9,
};

uint32_t s_quantWords[32] __attribute__((aligned(4)));
uint32_t s_scaleWords[32] __attribute__((aligned(4)));

bool waitDma(int ch) {
    for (uint32_t i = 0; i < c_dmaSpin; i++) {
        if ((DMA_CTRL[ch].CHCR & 0x01000000) == 0) return true;
    }
    return false;
}

uint8_t fromBCD(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

}  // namespace

namespace psyqo::paths {

enum class StrStreamState : uint8_t {
    IDLE,
    SETLOC,
    SETMODE,
    SETFILTER,
    DEMUTE,
    READS,
    STREAMING,
    PAUSE,
    PAUSE_ACK,
};

// The whole stream is one ReadS that runs until the player pauses it. Each INT1
// is one sector: the 44 bytes of headers come in by hand, and a video payload
// goes by DMA straight into its frame slot, at its place in the frame.
class StrStreamAction : public CDRomDevice::Action<StrStreamState> {
  public:
    StrStreamAction() : Action("StrStreamAction") {}
    void start(CDRomDevice *device, StrPlayer *player, uint32_t lba) {
        Kernel::assert(device->isIdle(), "StrPlayer: the CD-ROM device is busy");
        m_player = player;
        registerMe(device);
        setCallback([player](bool success) { player->cdDone(success); });
        setState(StrStreamState::SETLOC);
        eastl::atomic_signal_fence(eastl::memory_order_release);
        MSF msf(lba + 150);
        uint8_t bcd[3];
        msf.toBCD(bcd);
        Hardware::CDRom::Command.send(Hardware::CDRom::CDL::SETLOC, bcd[0], bcd[1], bcd[2]);
    }
    // Called from the main loop. The interrupt handler can also decide to pause,
    // so the state is checked and changed with interrupts off.
    void requestPause() {
        Kernel::fastEnterCriticalSection();
        if (getState() == StrStreamState::STREAMING) pauseFromISR();
        Kernel::fastLeaveCriticalSection();
    }
    void pauseFromISR() {
        setState(StrStreamState::PAUSE);
        Hardware::CDRom::Command.send(Hardware::CDRom::CDL::PAUSE);
    }
    bool dataReady(const CDRomDevice::Response &) override {
        if (getState() != StrStreamState::STREAMING) {
            Hardware::CDRom::Ctrl.throwAway();
            Hardware::CDRom::DataRequest = 0;
            Hardware::CDRom::InterruptControl.throwAway();
            return false;
        }
        // The sector cannot be pulled while the MDEC output DMA is moving: on
        // hardware the CD-ROM DMA then never completes. It waits in the drive's
        // buffer until the current MDEC slice lands, which is what Sony's
        // libraries do in 24-bit mode, and that interrupt pulls it.
        serviceSector();
        return false;
    }
    unsigned state() const { return unsigned(getState()) | (m_pending ? 0x100 : 0); }
    void servicePending() {
        if (!m_pending) return;
        m_pending = false;
        if (getState() == StrStreamState::STREAMING) serviceSector();
    }
    void serviceSector() {
        Hardware::CDRom::Ctrl.throwAway();
        Hardware::CDRom::DataRequest = 0;
        Hardware::CDRom::InterruptControl.throwAway();
        Hardware::CDRom::DataRequest = 0x80;
        Hardware::SBus::Dev5Ctrl = 0x20943;
        Hardware::SBus::ComCtrl = 0x132c;
        eastl::atomic_signal_fence(eastl::memory_order_acquire);

        volatile uint8_t *fifo = reinterpret_cast<volatile uint8_t *>(0xbf801802);
        uint8_t header[c_sectorHeader];
        // The data FIFO only fills after the request bit is set, so wait for
        // it to report data before reading the headers out by hand.
        volatile uint8_t *status = reinterpret_cast<volatile uint8_t *>(0xbf801800);
        for (unsigned i = 0; i < 1000 && !(*status & 0x40); i++);
        for (unsigned i = 0; i < c_sectorHeader; i++) header[i] = *fifo;
        uint32_t lba = (fromBCD(header[0]) * 60 + fromBCD(header[1])) * 75 + fromBCD(header[2]) - 150;

        uint8_t *dest = m_player->sectorDest(header, lba);
        if (dest) {
            // By hand rather than by DMA: a CD-ROM DMA competing with the MDEC
            // transfers does not complete on hardware. So, reads of the
            // data FIFO a byte at a time.
            for (unsigned i = 0; i < c_chunkPayload; i++) dest[i] = *fifo;
            m_player->sectorLanded();
        }
        if (lba + 1 >= m_player->m_end) pauseFromISR();
        eastl::atomic_signal_fence(eastl::memory_order_release);
    }
    bool complete(const CDRomDevice::Response &) override {
        if (getState() != StrStreamState::PAUSE_ACK) return false;
        setSuccess(true);
        return true;
    }
    bool acknowledge(const CDRomDevice::Response &) override {
        const StrPlayer::Options &o = m_player->m_options;
        switch (getState()) {
            case StrStreamState::SETLOC: {
                // Double speed, whole sectors minus sync so the MSF and the
                // subheader are visible, and XA-ADPCM to the SPU when wanted.
                uint8_t mode = 0x80 | 0x20;
                if (o.audio) mode |= 0x40;
                if (o.audio && o.xaFilter) mode |= 0x08;
                setState(StrStreamState::SETMODE);
                Hardware::CDRom::Command.send(Hardware::CDRom::CDL::SETMODE, mode);
                break;
            }
            case StrStreamState::SETMODE:
                if (o.audio && o.xaFilter) {
                    setState(StrStreamState::SETFILTER);
                    Hardware::CDRom::Command.send(Hardware::CDRom::CDL::SETFILTER, o.xaFile, o.xaChannel);
                    break;
                }
                [[fallthrough]];
            case StrStreamState::SETFILTER:
                setState(StrStreamState::DEMUTE);
                Hardware::CDRom::Command.send(Hardware::CDRom::CDL::UNMUTE);
                break;
            case StrStreamState::DEMUTE:
                setState(StrStreamState::READS);
                Hardware::CDRom::Command.send(Hardware::CDRom::CDL::READS);
                break;
            case StrStreamState::READS:
                setState(StrStreamState::STREAMING);
                break;
            case StrStreamState::PAUSE:
                setState(StrStreamState::PAUSE_ACK);
                break;
            default:
                break;
        }
        return false;
    }

  private:
    StrPlayer *m_player = nullptr;
    bool m_pending = false;
};

}  // namespace psyqo::paths

namespace {
psyqo::paths::StrStreamAction s_streamAction;
}

psyqo::paths::StrPlayer::~StrPlayer() {
    if (m_status == Status::PLAYING) stop();
    finish();
}

bool psyqo::paths::StrPlayer::play(uint32_t lba, uint32_t sectors) { return play(lba, sectors, Options{}); }

bool psyqo::paths::StrPlayer::play(uint32_t lba, uint32_t sectors, const Options &options) {
    Kernel::assert(m_status != Status::PLAYING, "StrPlayer: already playing");
    Kernel::assert(options.frameSlots >= 2, "StrPlayer: needs at least two frame slots");
    Kernel::assert(options.maxSectorsPerFrame >= 1 && options.maxSectorsPerFrame <= 32,
                   "StrPlayer: maxSectorsPerFrame must be 1..32");
    finish();
    m_options = options;
    m_stats = {};
    // Saved before any allocation: finish() restores them once m_slots exists.
    m_savedSpuCtrl = SPU_CTRL;
    m_savedCdLeft = SPU_VOL_CD_LEFT;
    m_savedCdRight = SPU_VOL_CD_RIGHT;
    const uint32_t slotBytes = options.maxSectorsPerFrame * c_chunkPayload;
    const uint32_t sliceHalfwords = (options.rgb24 ? 24 : 16) * options.displayHeight;
    m_slots = new Slot[options.frameSlots];
    m_slotMemory = new uint8_t[slotBytes * options.frameSlots + 4];
    m_rlBufs[0] = new uint16_t[options.rlHalfwords];
    m_rlBufs[1] = new uint16_t[options.rlHalfwords];
    m_slices[0] = new uint16_t[sliceHalfwords];
    m_slices[1] = new uint16_t[sliceHalfwords];
    if (!m_slots || !m_slotMemory || !m_rlBufs[0] || !m_rlBufs[1] || !m_slices[0] || !m_slices[1]) {
        finish();
        return false;
    }
    uint8_t *mem = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(m_slotMemory) + 3) & ~3);
    for (unsigned i = 0; i < options.frameSlots; i++) {
        m_slots[i].data = mem + i * slotBytes;
        m_slots[i].state = SLOT_FREE;
    }
    m_lba = lba;
    m_end = lba + sectors;
    m_lastLba = lba;
    m_writeSlot = m_readSlot = 0;
    m_filling = -1;
    m_skipFrame = 0xffffffff;
    m_haveRl = m_haveDecoded = m_failed = false;
    m_retries = 0;

    for (unsigned i = 0; i < 32; i++) {
        s_quantWords[i] = c_quant[(i * 4) & 63] | (c_quant[(i * 4 + 1) & 63] << 8) | (c_quant[(i * 4 + 2) & 63] << 16) |
                          (c_quant[(i * 4 + 3) & 63] << 24);
        s_scaleWords[i] = uint16_t(c_scale[i * 2]) | (uint32_t(uint16_t(c_scale[i * 2 + 1])) << 16);
    }
    DPCR |= 0x000000ff;
    m_dmaSlot = Kernel::registerDmaEvent(Kernel::DMA::MDECout, [this]() { mdecOutIRQ(); });
    // Enable the MDEC-out completion interrupt, leaving the flags alone.
    DICR = (DICR & 0xffffff) | 0x020000;
    m_vlcIdx = 0;
    m_decoding = false;
    mdecReset();
    m_needReset = false;

    if (options.audio) {
        SPU_VOL_CD_LEFT = 0x7fff;
        SPU_VOL_CD_RIGHT = 0x7fff;
        SPU_CTRL = m_savedSpuCtrl | 1;
    }

    hsyncs();
    waitVBlankEdge();
    m_lastPresentVsync = m_gpu.getLiveFrameCount();
    m_lastSectorFrame = m_lastPresentVsync;
    m_status = Status::PLAYING;
    m_cdRunning = true;
    s_streamAction.start(&m_cdrom, this, lba);
    return true;
}

uint8_t *psyqo::paths::StrPlayer::sectorDest(const uint8_t *header, uint32_t lba) {
    m_lastLba = lba;
    m_lastSectorFrame = m_gpu.getLiveFrameCount();
    const uint8_t *chunk = header + 12;
    if (lba < m_lba || lba >= m_end || le16(chunk) != 0x0160) {
        m_stats.otherSectors++;
        return nullptr;
    }
    m_stats.videoSectors++;
    const uint16_t index = le16(chunk + 4);
    const uint16_t chunks = le16(chunk + 6);
    const uint32_t frame = le32(chunk + 8);
    if (frame == m_skipFrame) return nullptr;
    if (m_filling >= 0 && m_slots[m_filling].frameNumber != frame) {
        // A new frame started while the previous one is still missing chunks.
        m_slots[m_filling].state = SLOT_FREE;
        m_filling = -1;
        m_stats.framesDropped++;
        m_stats.dropIncomplete++;
    }
    if (chunks == 0 || chunks > m_options.maxSectorsPerFrame || index >= chunks) {
        m_skipFrame = frame;
        m_stats.framesDropped++;
        return nullptr;
    }
    if (m_filling < 0) {
        Slot &s = m_slots[m_writeSlot];
        if (s.state != SLOT_FREE) {
            // The decoder is behind and the ring is full: this frame is lost.
            m_skipFrame = frame;
            m_stats.framesDropped++;
            m_stats.dropRingFull++;
            return nullptr;
        }
        s.frameNumber = frame;
        s.size = le32(chunk + 12);
        s.width = le16(chunk + 16);
        s.height = le16(chunk + 18);
        s.chunks = chunks;
        s.received = 0;
        s.state = SLOT_FILLING;
        m_filling = m_writeSlot;
    }
    Slot &s = m_slots[m_filling];
    const uint32_t bit = 1u << index;
    if (s.received & bit) return nullptr;
    m_landing = bit;
    return s.data + index * c_chunkPayload;
}

void psyqo::paths::StrPlayer::sectorLanded() {
    Slot &s = m_slots[m_filling];
    s.received |= m_landing;
    const uint32_t all = s.chunks == 32 ? 0xffffffff : (1u << s.chunks) - 1;
    if (s.received == all) {
        if (s.size > s.chunks * c_chunkPayload) s.size = s.chunks * c_chunkPayload;
        s.state = SLOT_READY;
        if (++m_writeSlot >= m_options.frameSlots) m_writeSlot = 0;
        m_filling = -1;
    }
}

void psyqo::paths::StrPlayer::cdDone(bool success) {
    if (!success) {
        m_stats.readErrors++;
        // A read error in the middle of the movie is retried from where the
        // stream got to; the frame it broke is dropped by the reassembly. One at
        // the very end is just the drive running off the file.
        if ((m_status == Status::PLAYING) && !m_stopping && (m_lastLba + 8 < m_end) && (m_retries < 3)) {
            m_retries++;
            s_streamAction.start(&m_cdrom, this, m_lastLba + 1);
            return;
        }
        if (m_lastLba + 8 < m_end && !m_stopping) m_failed = true;
    }
    m_cdRunning = false;
}

bool psyqo::paths::StrPlayer::vlcNext() {
    Slot &s = m_slots[m_readSlot];
    if (s.state != SLOT_READY) return false;
    bool ok = false;
    const uint32_t need = bsdecRlHalfwords(s.data, s.size);
    if (need && need <= m_options.rlHalfwords && s.width && s.height && !(s.width & 15) && !(s.height & 15) &&
        s.width <= m_options.displayWidth && s.height <= m_options.displayHeight) {
        const BsdecResult r = bsdecFrame(s.data, s.size, m_rlBufs[m_vlcIdx], m_options.rlHalfwords);
        if (bsdecUsable(r.error)) {
            ok = true;
            m_rlMeta[m_vlcIdx] = {r.mdecCommand, r.halfwords, s.width, s.height};
            m_rlIdx = m_vlcIdx;
            m_vlcIdx ^= 1;
            m_stats.lastFrameNumber = s.frameNumber;
        }
    }
    if (!ok) m_stats.framesRejected++;
    eastl::atomic_signal_fence(eastl::memory_order_release);
    s.state = SLOT_FREE;
    if (++m_readSlot >= m_options.frameSlots) m_readSlot = 0;
    m_haveRl = ok;
    return ok;
}

// Reset the MDEC and re-seat both tables. Needed after a decode that ended with
// input still queued, which is the normal case: the run-level stream is padded
// past the last block, and on hardware the MDEC stops consuming once the picture
// is out. It must not run while a VRAM upload shares the bus.
void psyqo::paths::StrPlayer::mdecReset() {
    MDEC1 = 0x80000000;
    MDEC1 = 0x60000000;
    MDEC0 = 0x40000001;
    DMA_CTRL[DMA_MDECIN].MADR = reinterpret_cast<uintptr_t>(s_quantWords);
    DMA_CTRL[DMA_MDECIN].BCR = 32 << 16 | 1;
    DMA_CTRL[DMA_MDECIN].CHCR = 0x01000201;
    waitDma(DMA_MDECIN);
    MDEC0 = 0x60000000;
    DMA_CTRL[DMA_MDECIN].MADR = reinterpret_cast<uintptr_t>(s_scaleWords);
    DMA_CTRL[DMA_MDECIN].BCR = 32 << 16 | 1;
    DMA_CTRL[DMA_MDECIN].CHCR = 0x01000201;
    waitDma(DMA_MDECIN);
}

// The MDEC output comes out one 16-pixel column at a time, because STR frames
// store their macroblocks column-major: a column is a contiguous 16xH strip,
// which is exactly one VRAM rectangle. Each column lands in one of two slice
// buffers and is uploaded while the MDEC produces the next one. All of it runs
// from interrupts, so the CPU is free to VLC-decode the next frame meanwhile.
void psyqo::paths::StrPlayer::startDecode() {
    const Rl &rl = m_rlMeta[m_rlIdx];
    const bool rgb24 = m_options.rgb24;
    m_colCount = rl.width / 16;
    m_colWords = (rgb24 ? 192 : 128) * (rl.height / 16);
    m_colHw = rgb24 ? 24 : 16;
    m_colHeight = rl.height;
    m_x0 = int16_t(((m_options.displayWidth - rl.width) / 2) * (rgb24 ? 3 : 2) / 2);
    m_y0 = int16_t((m_gpu.getParity() ? 256 : 0) + (m_options.displayHeight - rl.height) / 2);
    if (m_options.vramX >= 0) {
        m_x0 = m_options.vramX;
        m_y0 = m_options.vramY;
    }
    const uint32_t inWords = (rl.halfwords + 1) / 2;
    uint32_t command = rl.command;
    if (rgb24) command = (command & ~(3u << 27)) | (2u << 27);
    // A frame drawn as a texture: black without bit 15 would be see-through.
    if (m_options.vramX >= 0) command |= 1u << 25;

    if (m_needReset) {
        mdecReset();
        m_needReset = false;
    }
    m_decodeOk = waitDma(DMA_MDECIN) && waitDma(DMA_MDECOUT);
    if (!m_decodeOk) {
        m_needReset = true;
        m_decodeDone = true;
        m_decoding = true;
        return;
    }
    m_colArmed = 0;
    m_colDone = 0;
    m_upStarted = 0;
    m_upDone = 0;
    m_uploading = false;
    m_dma1Busy = false;
    m_decodeDone = false;
    m_decodeStart = m_gpu.getLiveFrameCount();
    m_decodeStartLine = hsyncs();
    m_decoding = true;
    eastl::atomic_signal_fence(eastl::memory_order_release);
    MDEC0 = command;
    // Both transfers are armed before either is waited on: the MDEC stops
    // asking for input once it holds a finished block, so serialising them
    // deadlocks on hardware.
    DMA_CTRL[DMA_MDECIN].MADR = reinterpret_cast<uintptr_t>(m_rlBufs[m_rlIdx]);
    // Sync mode 1: block size in the low half, block count in the high half.
    // The MDEC asks for 32 words at a time.
    DMA_CTRL[DMA_MDECIN].BCR = ((inWords + 31) / 32) << 16 | 32;
    DMA_CTRL[DMA_MDECIN].CHCR = 0x01000201;
    Kernel::fastEnterCriticalSection();
    pump();
    Kernel::fastLeaveCriticalSection();
}

// Moves the column pipeline along, from the MDEC output and GPU interrupts:
// a finished column is uploaded as soon as the GPU is free, and the MDEC is
// given the next slice buffer as soon as that buffer's upload is done. Only
// one VRAM upload is in flight at a time, which is all psyqo allows.
void psyqo::paths::StrPlayer::pump() {
    if (!m_uploading && m_upStarted < m_colDone) {
        const unsigned c = m_upStarted++;
        m_uploading = true;
        Rect region = {.pos = {{.x = int16_t(m_x0 + c * m_colHw), .y = m_y0}},
                       .size = {{.w = m_colHw, .h = int16_t(m_colHeight)}}};
        m_gpu.uploadToVRAM(
            m_slices[c & 1], region,
            [this]() {
                m_uploading = false;
                m_upDone++;
                pump();
            },
            DMA::FROM_ISR);
    }
    // Slice buffer (m_colArmed & 1) is free once the column that used it
    // two steps ago has been uploaded.
    if (!m_dma1Busy && m_colArmed < m_colCount && m_upDone + 2 > m_colArmed) {
        m_dma1Busy = true;
        DMA_CTRL[DMA_MDECOUT].MADR = reinterpret_cast<uintptr_t>(m_slices[m_colArmed & 1]);
        DMA_CTRL[DMA_MDECOUT].BCR = (m_colWords / 32) << 16 | 32;
        DMA_CTRL[DMA_MDECOUT].CHCR = 0x01000200;
        m_colArmed++;
    }
    if (m_upDone == m_colCount) m_decodeDone = true;
    eastl::atomic_signal_fence(eastl::memory_order_release);
}

void psyqo::paths::StrPlayer::mdecOutIRQ() {
    if (!m_decoding || !m_dma1Busy) return;
    m_dma1Busy = false;
    m_colDone++;
    s_streamAction.servicePending();
    pump();
}

// Called from the main loop once every column is in VRAM, or when the
// pipeline is stuck. Returns whether the back buffer holds the frame.
bool psyqo::paths::StrPlayer::finishDecode() {
    m_decoding = false;
    if (!m_decodeOk || !m_decodeDone) {
        abortDecode();
        return false;
    }
    if (DMA_CTRL[DMA_MDECIN].CHCR & 0x01000000) {
        DMA_CTRL[DMA_MDECIN].CHCR = 0;
        m_needReset = true;
    }
    return true;
}

// Timer 1 counts hblanks and wraps at 16 bits, so it is widened here. It is
// called at least once per frame, far more often than it wraps.
uint32_t psyqo::paths::StrPlayer::hsyncs() {
    const uint16_t v = COUNTERS[1].value;
    m_hsyncHigh += uint16_t(v - m_hsyncLast);
    m_hsyncLast = v;
    return m_hsyncHigh;
}

void psyqo::paths::StrPlayer::waitVBlankEdge() {
    const uint32_t f = m_gpu.getLiveFrameCount();
    while (m_gpu.getLiveFrameCount() == f) hsyncs();
    m_vblankRef = hsyncs();
}

void psyqo::paths::StrPlayer::present() {
    const uint32_t now = m_gpu.getLiveFrameCount();
    const uint32_t delta = now - m_lastPresentVsync;
    m_lastPresentVsync = now;
    if (m_stats.framesShown) m_stats.vsyncHistogram[delta >= 4 ? 3 : (delta ? delta - 1 : 0)]++;
    m_stats.framesShown++;
}

psyqo::paths::StrPlayer::Status psyqo::paths::StrPlayer::frame() {
    if (m_status != Status::PLAYING) return m_status;
    const unsigned linesPerFrame = m_gpu.getRefreshRate() == 60 ? 263 : 314;
    while (true) {
        // The interrupt handlers move the decode and stream state forward
        // while this loop waits on it.
        eastl::atomic_signal_fence(eastl::memory_order_acquire);
        hsyncs();
        if (!m_haveRl) vlcNext();
        if (m_decoding) {
            if (!m_decodeDone && (m_gpu.getLiveFrameCount() - m_decodeStart < m_gpu.getRefreshRate())) {
                Kernel::Internal::pumpCallbacks();
                checkStall();
                continue;
            }
            if (finishDecode()) {
                m_haveDecoded = true;
                const uint32_t lines = hsyncs() - m_decodeStartLine;
                m_stats.decodeLineHistogram[lines / 64 > 11 ? 11 : lines / 64]++;
                if (m_options.present == Present::ASAP) {
                    Hardware::GPU::Ctrl = 0x05000000 | ((m_gpu.getParity() ? 256 : 0) << 10);
                    const uint32_t line = (hsyncs() - m_vblankRef) % linesPerFrame;
                    m_stats.switchLineHistogram[line / 32 > 9 ? 9 : line / 32]++;
                }
            } else {
                m_stats.framesRejected++;
            }
            continue;
        }
        if (m_haveDecoded) {
            const bool streamEnded = !m_cdRunning;
            if (m_options.present == Present::STREAM) {
                if (!m_haveRl && !streamEnded) {
                    Kernel::Internal::pumpCallbacks();
                    checkStall();
                    continue;
                }
                waitVBlankEdge();
            }
            m_haveDecoded = false;
            present();
            return Status::PLAYING;
        }
        if (m_haveRl) {
            m_haveRl = false;
            startDecode();
            continue;
        }
        if (!m_cdRunning && m_slots[m_readSlot].state != SLOT_READY) {
            m_status = m_failed ? Status::FAILED : Status::ENDED;
            finish();
            return m_status;
        }
        Kernel::Internal::pumpCallbacks();
        checkStall();
    }
}

// If no sector has come in for a few seconds, the drive ran off the end of the
// file onto something that raises no interrupt (audio sectors, or nothing).
void psyqo::paths::StrPlayer::checkStall() {
    if (!m_cdRunning) return;
    if (m_stopping) return;
    if (m_gpu.getLiveFrameCount() - m_lastSectorFrame < 3 * m_gpu.getRefreshRate()) return;
    m_stopping = true;
    s_streamAction.requestPause();
}

// Stops the MDEC mid-frame and waits for the VRAM upload in flight, which reads
// from a slice buffer, so the buffers can be freed afterwards.
void psyqo::paths::StrPlayer::abortDecode() {
    m_decoding = false;
    DMA_CTRL[DMA_MDECOUT].CHCR = 0;
    DMA_CTRL[DMA_MDECIN].CHCR = 0;
    Kernel::fastEnterCriticalSection();
    m_dma1Busy = false;
    s_streamAction.servicePending();
    Kernel::fastLeaveCriticalSection();
    while (m_uploading) {
        Kernel::Internal::pumpCallbacks();
        eastl::atomic_signal_fence(eastl::memory_order_acquire);
    }
    m_needReset = true;
}

void psyqo::paths::StrPlayer::stop() {
    if (m_status != Status::PLAYING) return;
    m_stopping = true;
    s_streamAction.requestPause();
    while (m_cdRunning) {
        Kernel::Internal::pumpCallbacks();
        eastl::atomic_signal_fence(eastl::memory_order_acquire);
    }
    if (m_decoding) abortDecode();
    m_status = Status::ENDED;
    finish();
}

void psyqo::paths::StrPlayer::finish() {
    if (m_slots) {
        if (m_options.audio) {
            SPU_CTRL = m_savedSpuCtrl;
            SPU_VOL_CD_LEFT = m_savedCdLeft;
            SPU_VOL_CD_RIGHT = m_savedCdRight;
        }
    }
    delete[] m_slots;
    delete[] m_slotMemory;
    delete[] m_rlBufs[0];
    delete[] m_rlBufs[1];
    delete[] m_slices[0];
    delete[] m_slices[1];
    m_slots = nullptr;
    m_slotMemory = nullptr;
    m_rlBufs[0] = m_rlBufs[1] = nullptr;
    if (m_dmaSlot != ~0u) {
        Kernel::unregisterDmaEvent(m_dmaSlot);
        m_dmaSlot = ~0u;
        DICR = (DICR & 0xffffff) & ~0x020000;
    }
    m_slices[0] = m_slices[1] = nullptr;
    m_stopping = false;
}

void psyqo::paths::resolveMovie(ISO9660Parser &parser, eastl::string_view path,
                                eastl::function<void(bool, uint32_t, uint32_t)> &&callback) {
    auto *entry = new ISO9660Parser::DirEntry;
    parser.getDirentry(path, entry, [entry, callback = eastl::move(callback)](bool success) {
        const bool ok = success && entry->type == ISO9660Parser::DirEntry::FILE;
        const uint32_t lba = entry->LBA;
        const uint32_t sectors = (entry->size + 2047) / 2048;
        delete entry;
        callback(ok, ok ? lba : 0, ok ? sectors : 0);
    });
}

bool psyqo::paths::resolveMovie(const ArchiveManager &archive, eastl::string_view path, uint32_t *lba,
                                uint32_t *sectors) {
    const ArchiveManager::IndexEntry *entry = archive.getIndexEntry(path);
    if (!entry || entry->getCompressionMethod() != ArchiveManager::IndexEntry::Method::STREAM) return false;
    *lba = archive.getIndexEntrySectorStart(entry);
    *sectors = entry->getStreamSectors();
    return true;
}
