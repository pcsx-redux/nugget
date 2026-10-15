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

#pragma once

#include <EASTL/functional.h>
#include <EASTL/string_view.h>
#include <stdint.h>

#include "psyqo/cdrom-device.hh"
#include "psyqo/gpu.hh"
#include "psyqo/iso9660-parser.hh"

namespace psyqo::paths {

class ArchiveManager;

/**
 * @brief Streams an STR movie off the CD-ROM: MDEC video and interleaved XA audio.
 *
 * @details The player only knows about a run of sectors: a start LBA and a sector
 * count. Where they come from is the business of the helpers at the bottom of this
 * file, which resolve an ISO9660 path or an archive entry into that pair.
 *
 * The sectors are read in one continuous ReadS at double speed. Video sectors
 * (the 0x0160 chunk magic at the start of their user data) are reassembled into
 * frame slots straight from the CD-ROM interrupt, and XA-ADPCM audio sectors go to
 * the SPU through the drive, without ever touching RAM. Frames are VLC-decoded
 * with bsdec, run through the MDEC one 16-pixel column at a time, and each column
 * is uploaded to VRAM while the next one decodes.
 *
 * The player draws into psyqo's own double buffer, and presents through the
 * normal `frame()` / flip cycle: call `frame()` from the scene's `frame()`, and it
 * returns once the back buffer holds the next movie frame, so the flip that
 * follows shows it. That means a scene playing a movie does nothing else while it
 * plays, which is what the original libraries did too.
 *
 * The GPU has to be configured by the application for the depth the player
 * outputs: 24 bits per pixel by default, 15 bits with `Options::rgb24 = false`.
 * In 24 bits, a 320 pixels wide frame occupies 480 halfwords of VRAM per buffer.
 *
 * The CD-ROM device is owned by the player for the duration of the playback.
 */
class StrPlayer {
  public:
    enum class Present : uint8_t {
        // Show a decoded frame at the first vblank after the NEXT frame has
        // arrived off the disc, the way Sony's libraries do: the disc sets the
        // rate, and the vblank wait keeps the swap out of the visible picture.
        // A 320x240 24-bit frame takes about 480 scanlines to decode, so a
        // 30 fps movie holds 30 fps at 60 Hz but is shown at 25 fps at 50 Hz,
        // dropping one frame in six.
        STREAM,
        // Switch the display to a frame the moment it is decoded, wherever the
        // beam is. Keeps 30 fps at 50 Hz, but the swap lands anywhere in the
        // picture and tears visibly on fast motion.
        ASAP,
    };

    struct Options {
        bool rgb24 = true;
        bool audio = true;
        // Setfilter: only play the XA channel matching file/channel. When false,
        // the drive plays every audio sector it meets.
        bool xaFilter = false;
        uint8_t xaFile = 0;
        uint8_t xaChannel = 0;
        Present present = Present::STREAM;
        // Number of frames the reassembly ring holds, and the sector budget of a
        // single frame. A frame using more chunks than this is dropped.
        uint8_t frameSlots = 3;
        uint8_t maxSectorsPerFrame = 12;
        // Run-level buffer for bsdec, in halfwords. A frame needing more is dropped.
        uint32_t rlHalfwords = 20480;
        // Size of the display the frame is centered in, in pixels.
        uint16_t displayWidth = 320;
        uint16_t displayHeight = 240;
    };

    enum class Status : uint8_t { IDLE, PLAYING, ENDED, FAILED };

    struct Stats {
        uint32_t framesShown = 0;
        uint32_t framesDropped = 0;   // lost in reassembly: missing chunks, or no free slot
        uint32_t dropIncomplete = 0;  // of which a new frame started before every chunk arrived
        uint32_t dropRingFull = 0;    // of which the decoder was behind and no slot was free
        uint32_t framesRejected = 0;  // complete, but bsdec or the MDEC refused them
        uint32_t videoSectors = 0;
        uint32_t otherSectors = 0;
        uint32_t readErrors = 0;
        uint32_t lastFrameNumber = 0;
        // Vsyncs between two consecutive presented frames, 1, 2, 3, and 4 or more.
        uint32_t vsyncHistogram[4] = {};
        // Scanline the display switch happened on, counted from the start of
        // vblank, in buckets of 32 lines. Only filled in Present::ASAP.
        uint32_t switchLineHistogram[10] = {};
        // Scanlines from the start of a decode to its last column landing in
        // VRAM, in buckets of 64 lines, the last one 704 and up.
        uint32_t decodeLineHistogram[12] = {};
    };

    StrPlayer(CDRomDevice &cdrom, GPU &gpu) : m_cdrom(cdrom), m_gpu(gpu) {}
    ~StrPlayer();

    /**
     * @brief Starts playing the movie in sectors [lba, lba + sectors).
     *
     * @details The CD-ROM device must be idle. Returns false if the buffers
     * could not be allocated. Playback actually happens in `frame()`.
     */
    bool play(uint32_t lba, uint32_t sectors, const Options &options);
    bool play(uint32_t lba, uint32_t sectors);

    /**
     * @brief Drives the playback, to be called from the scene's `frame()`.
     *
     * @details Blocks until the back buffer holds the next frame to present, then
     * returns PLAYING. Returns ENDED once the stream is exhausted and every frame
     * was shown, FAILED if the drive gave up, and IDLE when not playing.
     */
    Status frame();

    /**
     * @brief Stops the playback early. The drive is paused and the buffers freed.
     */
    void stop();

    const Stats &stats() const { return m_stats; }

  private:
    struct Slot {
        uint8_t *data;
        uint32_t frameNumber;
        uint32_t size;
        uint32_t received;  // bitmask of chunks
        uint16_t chunks;
        uint16_t width;
        uint16_t height;
        volatile uint8_t state;
    };
    friend class StrStreamAction;
    uint8_t *sectorDest(const uint8_t *header, uint32_t lba);
    void sectorLanded();
    void cdDone(bool success);
    void present();
    void checkStall();
    bool vlcNext();
    void startDecode();
    void pump();
    void mdecOutIRQ();
    bool finishDecode();
    void mdecReset();
    void finish();
    uint32_t hsyncs();
    void waitVBlankEdge();

    CDRomDevice &m_cdrom;
    GPU &m_gpu;
    Options m_options;
    Stats m_stats;
    Slot *m_slots = nullptr;
    uint8_t *m_slotMemory = nullptr;
    struct Rl {
        uint32_t command;
        uint32_t halfwords;
        uint16_t width;
        uint16_t height;
    };
    uint16_t *m_rlBufs[2] = {nullptr, nullptr};
    Rl m_rlMeta[2] = {};
    unsigned m_rlIdx = 0;
    unsigned m_vlcIdx = 0;
    unsigned m_dmaSlot = ~0u;
    uint32_t m_colWords = 0;
    uint32_t m_decodeStart = 0;
    uint32_t m_decodeStartLine = 0;
    uint16_t m_colHeight = 0;
    int16_t m_colHw = 0;
    int16_t m_x0 = 0;
    int16_t m_y0 = 0;
    volatile uint8_t m_colCount = 0;
    volatile uint8_t m_colArmed = 0;
    volatile uint8_t m_colDone = 0;
    volatile uint8_t m_upStarted = 0;
    volatile uint8_t m_upDone = 0;
    volatile bool m_uploading = false;
    volatile bool m_dma1Busy = false;
    volatile bool m_decodeDone = false;
    volatile bool m_decoding = false;
    bool m_decodeOk = false;
    uint16_t *m_slices[2] = {nullptr, nullptr};
    uint32_t m_lba = 0;
    uint32_t m_end = 0;
    uint32_t m_lastLba = 0;
    unsigned m_writeSlot = 0;
    unsigned m_readSlot = 0;
    int m_filling = -1;
    uint32_t m_skipFrame = 0xffffffff;
    bool m_haveRl = false;
    bool m_haveDecoded = false;
    bool m_needReset = false;
    bool m_failed = false;
    volatile bool m_cdRunning = false;
    volatile uint32_t m_lastSectorFrame = 0;
    uint32_t m_lastPresentVsync = 0;
    uint32_t m_hsyncHigh = 0;
    uint16_t m_hsyncLast = 0;
    uint32_t m_vblankRef = 0;
    uint32_t m_landing = 0;
    uint16_t m_savedSpuCtrl = 0;
    uint16_t m_savedCdLeft = 0;
    uint16_t m_savedCdRight = 0;
    uint8_t m_retries = 0;
    volatile bool m_stopping = false;
    Status m_status = Status::IDLE;
};

/**
 * @brief Resolves an ISO9660 path into the sector run of a movie.
 *
 * @details The callback receives the start LBA and the sector count, or false if
 * the path does not exist.
 */
void resolveMovie(ISO9660Parser &parser, eastl::string_view path,
                  eastl::function<void(bool success, uint32_t lba, uint32_t sectors)> &&callback);

/**
 * @brief Resolves an archive entry into the sector run of a movie.
 *
 * @details The entry must use the STREAM method: raw sectors, stored as-is, whose
 * count is held in the entry's first 32-bit field. Returns false otherwise.
 */
bool resolveMovie(const ArchiveManager &archive, eastl::string_view path, uint32_t *lba, uint32_t *sectors);

}  // namespace psyqo::paths
