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

#include <stdint.h>

// A FastTracker 2 module player for the SPU. Songs are converted with the xmconv tool from
// PCSX-Redux, which writes two files: the song (patterns, instruments, sample headers), which stays
// in main RAM while playing, and the samples, which are uploaded to SPU RAM by XM_Load and can be
// freed afterwards.
//
// Each XM channel plays on the SPU voice with the same number. Voices from XM_Channels to 23 are
// left alone, and can be used for sound effects with XM_PlaySoundEffect.

// Song file format, version 1. All fields are little endian. Offsets are from the start of the file.
struct XMSongHeader {
    char magic[4];  // "PSXM"
    uint16_t version;
    uint16_t headerSize;
    uint8_t channels;
    uint8_t flags;  // bit 0: linear frequency table, otherwise Amiga periods
    uint8_t speed;  // initial ticks per row
    uint8_t bpm;    // initial tempo
    uint16_t songLength;
    uint16_t restart;
    uint16_t numPatterns;
    uint16_t numInstruments;
    uint16_t numSamples;
    uint16_t reserved;
    uint32_t sampleBytes;  // size of the sample file
    uint32_t ordersOffset;       // songLength bytes, pattern numbers
    uint32_t patternsOffset;     // numPatterns of struct XMPatternEntry
    uint32_t instrumentsOffset;  // numInstruments of struct XMInstrument
    uint32_t samplesOffset;      // numSamples of struct XMSample
    char title[20];
};

// Pattern data uses the XM packing: a byte with bit 7 set is a mask of which of note, instrument,
// volume, effect and parameter follow; otherwise that byte is the note, and the four others follow.
struct XMPatternEntry {
    uint32_t offset;
    uint16_t rows;
    uint16_t size;
};

struct XMEnvelope {
    uint8_t count, sustain, loopStart, loopEnd, flags;  // flags: bit 0 on, bit 1 sustain, bit 2 loop
};

struct XMInstrument {
    uint8_t keymap[96];          // sample index for each note, 0xff for none
    uint16_t volPoints[12][2];   // tick, value 0..64
    uint16_t panPoints[12][2];   // tick, value 0..64
    struct XMEnvelope vol, pan;  // 10 bytes
    uint8_t vibType, vibSweep, vibDepth, vibRate;
    uint16_t fadeout;
};

struct XMSample {
    uint32_t spuOffset;   // bytes into the sample file
    uint32_t spuLength;   // bytes
    uint32_t length;      // samples, at the converted rate
    uint32_t loopStart;   // samples, at the converted rate
    uint32_t loopLength;  // samples; for a ping-pong loop, the unfolded period
    uint16_t pad;         // silent samples written before the start, to align the loop
    uint8_t flags;        // bit 0 loop, bit 1 ping-pong
    uint8_t rateShift;    // the sample was resampled to 1 / (1 << rateShift) of its rate
    uint8_t volume;       // 0..64
    int8_t finetune;
    uint8_t pan;
    int8_t relnote;
    uint32_t reserved;
};

extern unsigned XM_Channels;
extern unsigned XM_SongLength;
extern unsigned XM_CurrentOrder;
extern unsigned XM_CurrentPattern;
extern unsigned XM_CurrentRow;
extern unsigned XM_Speed;
extern unsigned XM_Tick;
extern unsigned XM_BPM;
extern unsigned XM_GlobalVolume;

// Number of hblanks to wait before the next call to XM_Poll. It changes from one call to the next, as the
// fractional part of the tick length is carried over, so read it again after every call.
extern uint32_t XM_hblanks;

// Checks the song file. Returns the number of channels, or 0 if the file isn't a song this player can
// play.
unsigned XM_Check(const void* song);

// Loads the song and uploads the samples to SPU RAM, then gets ready to play. The song has to stay in
// memory, 4-bytes aligned, while it plays; the samples can be freed once this returns. If samples is
// NULL, they are considered already uploaded. Returns the number of channels, or 0 on error.
unsigned XM_Load(const void* song, const uint8_t* samples);

// Call this at the rate given by XM_hblanks to play.
void XM_Poll(void);

// Master music volume, 0 to 65535, defaults to 16384. Not reset by XM_Load.
void XM_SetMusicVolume(uint32_t volume);

// Plays a note from the song's instruments on a voice the song doesn't use. Instrument is 1-based,
// note is 1 (C-0) to 96, volume is 0 to 64, and is not affected by the music volume.
void XM_PlaySoundEffect(unsigned voice, unsigned instrument, unsigned note, unsigned volume);

// Resets the SPU and silences everything.
void XM_Silence(void);
