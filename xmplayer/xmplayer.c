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

#include "xmplayer/xmplayer.h"

#include <stddef.h>
#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/spu.h"

// Written from the XM format document and checked against reference renders of the same songs.

_Static_assert(sizeof(struct XMSongHeader) == 64, "song header");
_Static_assert(sizeof(struct XMInstrument) == 208, "instrument record");
_Static_assert(sizeof(struct XMSample) == 32, "sample record");

#define SPU_BASE 0x1010

struct XMChannel {
    const struct XMInstrument* instrument;
    const struct XMSample* sample;
    int32_t period;        // FT2 period units
    int32_t targetPeriod;  // tone portamento
    int32_t outPeriod;     // period with vibrato and arpeggio, for this tick
    int16_t realNote;      // 0-based note including the sample's relative note
    uint8_t note;          // last note, 1..96
    uint8_t volume;        // 0..64
    int16_t outVolume;     // volume with tremolo, for this tick
    uint8_t pan;           // 0..255
    uint8_t keyOn;
    uint16_t volEnvTick, panEnvTick;
    int32_t fadeVolume;  // 32768 down to 0 after a key off
    uint16_t autoVibPos, autoVibRamp;
    uint8_t vibPos, vibSpeed, vibDepth, vibWave;
    uint8_t tremPos, tremSpeed, tremDepth, tremWave;
    uint8_t tremorPos;
    // effect memories
    uint8_t portaUp, portaDown, portaSpeed, volSlide, finePortaUp, finePortaDown, fineVolUp, fineVolDown;
    uint8_t globalSlide, panSlide, retrig, tremor, offset, xfinePortaUp, xfinePortaDown;
    // this row's cell
    uint8_t cellNote, cellInstrument, cellVolume, cellEffect, cellParam;
    uint32_t startAddress;  // SPU address of the next key on
};

static struct XMChannel s_channels[24];
static const uint8_t* s_song;
static const struct XMSongHeader* s_header;
static const uint8_t* s_orders;
static const struct XMPatternEntry* s_patterns;
static const struct XMInstrument* s_instruments;
static const struct XMSample* s_samples;
static const uint8_t* s_rowPointer;
static unsigned s_rows;
static unsigned s_patternDelay, s_inDelay;
static int s_jumpOrder, s_breakRow;
static unsigned s_loopRow, s_loopCount;
static const uint8_t* s_loopPointer;
static uint32_t s_keyOn;
static uint32_t s_masterVolume = 16384;
static uint16_t s_exp2[768];  // 2^(i/768) in Q15
static uint16_t s_panLaw[257];  // sqrt(i/256) in Q15

unsigned XM_Channels;
unsigned XM_SongLength;
unsigned XM_CurrentOrder;
unsigned XM_CurrentPattern;
unsigned XM_CurrentRow;
unsigned XM_Speed;
unsigned XM_Tick;
unsigned XM_BPM;
unsigned XM_GlobalVolume;
uint32_t XM_hblanks;

static const uint8_t s_sine[32] = {
    0x00, 0x18, 0x31, 0x4a, 0x61, 0x78, 0x8d, 0xa1, 0xb4, 0xc5, 0xd4, 0xe0, 0xeb, 0xf4, 0xfa, 0xfd,
    0xff, 0xfd, 0xfa, 0xf4, 0xeb, 0xe0, 0xd4, 0xc5, 0xb4, 0xa1, 0x8d, 0x78, 0x61, 0x4a, 0x31, 0x18,
};

static unsigned isqrt(uint32_t v) {
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

static void initTables(void) {
    if (s_exp2[0]) return;
    // 2^(1/768) in Q30, iterated in Q30 so the rounding error stays far below a Q15 step.
    uint32_t v = 1u << 30;
    for (unsigned i = 0; i < 768; i++) {
        s_exp2[i] = v >> 15;
        v = ((uint64_t)v * 1074711351u) >> 30;
    }
    for (unsigned i = 0; i <= 256; i++) s_panLaw[i] = isqrt(i << 22);
}

// SPU helpers

static void SPUInit(void) {
    DPCR |= 0x000b0000;
    SPU_VOL_MAIN_LEFT = 0x3800;
    SPU_VOL_MAIN_RIGHT = 0x3800;
    SPU_CTRL = 0;
    SPU_KEY_ON_LOW = 0;
    SPU_KEY_ON_HIGH = 0;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_RAM_DTC = 4;
    SPU_VOL_CD_LEFT = 0;
    SPU_VOL_CD_RIGHT = 0;
    SPU_PITCH_MOD_LOW = 0;
    SPU_PITCH_MOD_HIGH = 0;
    SPU_NOISE_EN_LOW = 0;
    SPU_NOISE_EN_HIGH = 0;
    SPU_REVERB_EN_LOW = 0;
    SPU_REVERB_EN_HIGH = 0;
    SPU_VOL_EXT_LEFT = 0;
    SPU_VOL_EXT_RIGHT = 0;
    SPU_CTRL = 0x8000;
}

static void SPUResetVoice(unsigned v) {
    SPU_VOICES[v].volumeLeft = 0;
    SPU_VOICES[v].volumeRight = 0;
    SPU_VOICES[v].sampleRate = 0;
    SPU_VOICES[v].sampleStartAddr = 0;
    SPU_VOICES[v].adsrLo = 0x000f;
    SPU_VOICES[v].currentVolume = 0;
    SPU_VOICES[v].sampleRepeatAddr = 0;
    SPU_VOICES[v].adsrHi = 0x0000;
}

static void SPUUpload(uint32_t spuAddr, const uint8_t* data, uint32_t size) {
    uint32_t bcr = size >> 6;
    if (size & 0x3f) bcr++;
    bcr <<= 16;
    bcr |= 0x10;
    SPU_CTRL &= ~0x0030;
    while ((SPU_STATUS & 0x0030) != 0);
    SPU_RAM_DTA = spuAddr >> 3;
    SPU_CTRL = (SPU_CTRL & ~0x0030) | 0x0020;
    while ((SPU_STATUS & 0x0030) != 0x0020);
    SBUS_DEV4_CTRL &= ~0x0f000000;
    DMA_CTRL[DMA_SPU].MADR = (uint32_t)data;
    DMA_CTRL[DMA_SPU].BCR = bcr;
    DMA_CTRL[DMA_SPU].CHCR = 0x01000201;
    while ((DMA_CTRL[DMA_SPU].CHCR & 0x01000000) != 0);
    // DMA done is not SPU write done: SPUSTAT bit 7 sets once the data has landed. The bound
    // covers emulators that never set it.
    for (unsigned i = 0; i < 65536 && (SPU_STATUS & 0x0080) == 0; i++);
    SPU_CTRL &= ~0x0030;
}

static uint32_t s_hblankStep, s_hblankFraction;

static void advanceHblanks(void) {
    s_hblankFraction += s_hblankStep;
    XM_hblanks = s_hblankFraction >> 16;
    s_hblankFraction &= 0xffff;
}

static void setBPM(unsigned bpm) {
    XM_BPM = bpm;
    uint32_t status = GPU_STATUS;
    int isPalConsole = *((const char*)0xbfc7ff52) == 'E';
    int isPal = (status & 0x00100000) != 0;
    uint32_t base;
    if (isPal && isPalConsole) {
        base = 39062;
    } else if (isPal && !isPalConsole) {
        base = 39422;
    } else if (!isPal && isPalConsole) {
        base = 38977;
    } else {
        base = 39336;
    }
    // Q16, so the fraction is carried from tick to tick instead of truncated: a whole number of
    // hblanks per tick runs up to 0.3% fast.
    s_hblankStep = (base << 16) / bpm;
    s_hblankFraction = 0;
    advanceHblanks();
}

// Pitch math. Periods are FT2's: linear periods are 64 per semitone, 7680 - note * 64 - finetune / 2;
// Amiga periods are 1712 for C-4, the ProTracker periods times 4.

static int linear(void) { return s_header->flags & 1; }

static int32_t notePeriod(int realNote, int finetune) {
    int32_t x = realNote * 64 + finetune / 2;  // 1/768 octave above C-0
    if (linear()) return 7680 - x;
    // 1712 * 2^((3072 - x) / 768)
    int32_t e = 3072 - x;
    int32_t octave = e >= 0 ? e / 768 : -((-e + 767) / 768);
    uint32_t p = (1712u * s_exp2[e - octave * 768]) >> 15;
    return octave >= 0 ? (int32_t)(p << octave) : (int32_t)(p >> -octave);
}

static uint16_t periodToPitch(int32_t period, unsigned rateShift) {
    uint32_t pitch;
    if (period < 1) period = 1;
    if (linear()) {
        // 8363 * 2^((4608 - period) / 768) Hz, times 4096 / 44100
        int32_t x = 4608 - period;
        int32_t octave = x >= 0 ? x / 768 : -((-x + 767) / 768);
        pitch = (49708u * s_exp2[x - octave * 768]) >> 21;  // 776.68 * 2^(frac / 768)
        octave -= rateShift;
        if (octave > 6) return 0x3fff;
        pitch = octave >= 0 ? pitch << octave : pitch >> -octave;
    } else {
        // 8363 * 1712 / period Hz, times 4096 / 44100
        pitch = (1329803u / (uint32_t)period) >> rateShift;
    }
    return pitch > 0x3fff ? 0x3fff : pitch;
}

static int32_t arpeggioPeriod(struct XMChannel* ch, unsigned semitones) {
    if (linear()) return ch->period - semitones * 64;
    return notePeriod(ch->realNote + semitones, ch->sample ? ch->sample->finetune : 0);
}

// Envelopes

static unsigned envelopeValue(const uint16_t (*points)[2], const struct XMEnvelope* env, unsigned tick) {
    unsigned i = 0;
    while ((i + 1) < env->count && points[i + 1][0] <= tick) i++;
    if ((i + 1) >= env->count || points[i + 1][0] == points[i][0]) return points[i][1];
    int32_t t0 = points[i][0], t1 = points[i + 1][0];
    int32_t v0 = points[i][1], v1 = points[i + 1][1];
    return v0 + (v1 - v0) * ((int32_t)tick - t0) / (t1 - t0);
}

static uint16_t envelopeAdvance(const uint16_t (*points)[2], const struct XMEnvelope* env, uint16_t tick,
                                int keyOn) {
    if ((env->flags & 2) && keyOn && tick == points[env->sustain][0]) return tick;
    tick++;
    if ((env->flags & 4) && tick >= points[env->loopEnd][0]) tick = points[env->loopStart][0];
    return tick;
}

static void keyOff(struct XMChannel* ch) {
    ch->keyOn = 0;
    if (!ch->instrument || !(ch->instrument->vol.flags & 1)) ch->volume = 0;
}

// Effects

static void volumeSlide(struct XMChannel* ch, uint8_t param) {
    int v = ch->volume;
    if (param & 0xf0) {
        v += param >> 4;
    } else {
        v -= param & 0x0f;
    }
    ch->volume = v < 0 ? 0 : v > 64 ? 64 : v;
}

static void tonePortamento(struct XMChannel* ch) {
    int32_t step = ch->portaSpeed * 4;
    if (ch->period < ch->targetPeriod) {
        ch->period += step;
        if (ch->period > ch->targetPeriod) ch->period = ch->targetPeriod;
    } else if (ch->period > ch->targetPeriod) {
        ch->period -= step;
        if (ch->period < ch->targetPeriod) ch->period = ch->targetPeriod;
    }
}

static int waveform(uint8_t wave, uint8_t pos) {
    int v;
    switch (wave & 3) {
        case 1:
            v = 255 - (pos & 31) * 8;
            v = (pos & 32) ? -v : v;
            return -v;
        case 2:
            return (pos & 32) ? -255 : 255;
        default:
            v = s_sine[pos & 31];
            return (pos & 32) ? -v : v;
    }
}

static void vibrato(struct XMChannel* ch) {
    ch->outPeriod = ch->period + ((waveform(ch->vibWave, ch->vibPos) * ch->vibDepth) >> 5);
    ch->vibPos = (ch->vibPos + ch->vibSpeed) & 63;
}

static void tremolo(struct XMChannel* ch) {
    int v = ch->volume + ((waveform(ch->tremWave, ch->tremPos) * ch->tremDepth) >> 6);
    ch->outVolume = v < 0 ? 0 : v > 64 ? 64 : v;
    ch->tremPos = (ch->tremPos + ch->tremSpeed) & 63;
}

// Rxy, multi retrig. Not implemented: this is the hook it plugs into, called on every tick of a row
// carrying R. The render comparison for it is xmplayer/tests/retrig, which fails until this is written.
static void multiRetrig(struct XMChannel* ch, unsigned voice) {
    (void)ch;
    (void)voice;
}

static void triggerNote(struct XMChannel* ch, unsigned voice, unsigned offset) {
    const struct XMSample* s = ch->sample;
    if (!s || !s->spuLength) return;
    uint32_t pos = offset >> s->rateShift;
    uint32_t index;
    if ((s->flags & 1) && pos >= s->loopStart) {
        // the converter keeps nothing past the loop end, so offsets beyond it wrap into the loop
        index = s->loopStart + (pos - s->loopStart) % s->loopLength + s->pad;
    } else if (pos >= s->length) {
        // past the end of a one-shot sample: start on its trailing silent block
        index = (s->spuLength / 16 - 1) * 28;
    } else {
        index = pos + s->pad;
    }
    ch->startAddress = SPU_BASE + s->spuOffset + (index / 28) * 16;
    SPU_VOICES[voice].sampleStartAddr = ch->startAddress >> 3;
    s_keyOn |= 1u << voice;
}

static void resetInstrument(struct XMChannel* ch) {
    if (ch->sample) {
        ch->volume = ch->sample->volume;
        ch->pan = ch->sample->pan;
    }
    ch->keyOn = 1;
    ch->fadeVolume = 32768;
    ch->volEnvTick = ch->panEnvTick = 0;
    ch->autoVibPos = ch->autoVibRamp = 0;
    ch->tremorPos = 0;
}

static void readCell(struct XMChannel* ch) {
    const uint8_t* p = s_rowPointer;
    uint8_t b = *p++;
    ch->cellNote = ch->cellInstrument = ch->cellVolume = ch->cellEffect = ch->cellParam = 0;
    if (b & 0x80) {
        if (b & 1) ch->cellNote = *p++;
        if (b & 2) ch->cellInstrument = *p++;
        if (b & 4) ch->cellVolume = *p++;
        if (b & 8) ch->cellEffect = *p++;
        if (b & 16) ch->cellParam = *p++;
    } else {
        ch->cellNote = b;
        ch->cellInstrument = *p++;
        ch->cellVolume = *p++;
        ch->cellEffect = *p++;
        ch->cellParam = *p++;
    }
    s_rowPointer = p;
}

static const uint8_t* rowAt(unsigned pattern, unsigned row) {
    const uint8_t* p = s_song + s_patterns[pattern].offset;
    for (unsigned r = 0; r < row; r++) {
        for (unsigned c = 0; c < XM_Channels; c++) {
            uint8_t b = *p++;
            if (b & 0x80) {
                p += (b & 1) + ((b >> 1) & 1) + ((b >> 2) & 1) + ((b >> 3) & 1) + ((b >> 4) & 1);
            } else {
                p += 4;
            }
        }
    }
    return p;
}

static void setPosition(unsigned order, unsigned row) {
    if (order >= XM_SongLength) order = s_header->restart;
    XM_CurrentOrder = order;
    XM_CurrentPattern = s_orders[order];
    s_rows = XM_CurrentPattern < s_header->numPatterns ? s_patterns[XM_CurrentPattern].rows : 64;
    if (row >= s_rows) row = 0;
    XM_CurrentRow = row;
    s_rowPointer = XM_CurrentPattern < s_header->numPatterns ? rowAt(XM_CurrentPattern, row) : NULL;
}

// Note, instrument and volume column handling, at the tick the note plays (0, or the ED delay).
static void startRow(struct XMChannel* ch, unsigned voice) {
    unsigned note = ch->cellNote, instrument = ch->cellInstrument;
    int porta = ch->cellEffect == 3 || ch->cellEffect == 5 || (ch->cellVolume >> 4) == 0xf;
    if (instrument && instrument <= s_header->numInstruments) ch->instrument = &s_instruments[instrument - 1];
    if (note == 97) {
        keyOff(ch);
    } else if (note && ch->instrument) {
        uint8_t si = ch->instrument->keymap[note - 1];
        const struct XMSample* s = si < s_header->numSamples ? &s_samples[si] : NULL;
        if (s) {
            int realNote = (int)note - 1 + s->relnote;
            if (realNote >= 0 && realNote < 119) {
                int32_t period = notePeriod(realNote, s->finetune);
                if (porta && ch->sample) {
                    ch->targetPeriod = period;
                } else {
                    ch->sample = s;
                    ch->note = note;
                    ch->realNote = realNote;
                    ch->period = ch->targetPeriod = period;
                    if (!(ch->vibWave & 4)) ch->vibPos = 0;
                    if (!(ch->tremWave & 4)) ch->tremPos = 0;
                    unsigned offset = 0;
                    if (ch->cellEffect == 9) {
                        if (ch->cellParam) ch->offset = ch->cellParam;
                        offset = ch->offset << 8;
                    }
                    triggerNote(ch, voice, offset);
                }
            }
        }
    }
    if (instrument && note != 97) resetInstrument(ch);
    uint8_t v = ch->cellVolume;
    switch (v >> 4) {
        case 1: case 2: case 3: case 4:
            ch->volume = v - 0x10;
            break;
        case 5:
            if (v <= 0x50) ch->volume = 64;
            break;
        case 8:
            volumeSlide(ch, v & 15);
            break;
        case 9:
            volumeSlide(ch, (v & 15) << 4);
            break;
        case 10:
            ch->vibSpeed = (v & 15) << 2;
            break;
        case 11:
            if (v & 15) ch->vibDepth = v & 15;
            break;
        case 12:
            ch->pan = (v & 15) << 4;
            break;
        case 15:
            if (v & 15) ch->portaSpeed = (v & 15) << 4;
            break;
    }
}

static void rowEffects(struct XMChannel* ch, unsigned voice) {
    uint8_t p = ch->cellParam, x = p >> 4, y = p & 15;
    switch (ch->cellEffect) {
        case 1:
            if (p) ch->portaUp = p;
            break;
        case 2:
            if (p) ch->portaDown = p;
            break;
        case 3:
            if (p) ch->portaSpeed = p;
            break;
        case 4:
            if (x) ch->vibSpeed = x << 2;
            if (y) ch->vibDepth = y;
            break;
        case 5:
        case 6:
        case 10:
            if (p) ch->volSlide = p;
            break;
        case 7:
            if (x) ch->tremSpeed = x << 2;
            if (y) ch->tremDepth = y;
            break;
        case 8:
            ch->pan = p;
            break;
        case 11:
            s_jumpOrder = p;
            s_breakRow = s_breakRow < 0 ? 0 : s_breakRow;
            break;
        case 12:
            ch->volume = p > 64 ? 64 : p;
            break;
        case 13:
            s_breakRow = x * 10 + y;
            if (s_jumpOrder < 0) s_jumpOrder = XM_CurrentOrder + 1;
            break;
        case 14:
            switch (x) {
                case 1:
                    if (y) ch->finePortaUp = y;
                    ch->period -= ch->finePortaUp * 4;
                    break;
                case 2:
                    if (y) ch->finePortaDown = y;
                    ch->period += ch->finePortaDown * 4;
                    break;
                case 4:
                    ch->vibWave = y;
                    break;
                case 6:
                    if (y == 0) {
                        s_loopRow = XM_CurrentRow;
                    } else if (s_loopCount == 0) {
                        s_loopCount = y;
                        s_breakRow = s_loopRow;
                        s_jumpOrder = XM_CurrentOrder;
                    } else if (--s_loopCount) {
                        s_breakRow = s_loopRow;
                        s_jumpOrder = XM_CurrentOrder;
                    }
                    break;
                case 7:
                    ch->tremWave = y;
                    break;
                case 10:
                    if (y) ch->fineVolUp = y;
                    volumeSlide(ch, ch->fineVolUp << 4);
                    break;
                case 11:
                    if (y) ch->fineVolDown = y;
                    volumeSlide(ch, ch->fineVolDown);
                    break;
                case 12:
                    if (y == 0) ch->volume = 0;
                    break;
                case 14:
                    if (!s_inDelay) s_patternDelay = y;
                    break;
            }
            break;
        case 15:
            if (p == 0) break;
            if (p < 32) {
                XM_Speed = p;
            } else {
                setBPM(p);
            }
            break;
        case 16:
            XM_GlobalVolume = p > 64 ? 64 : p;
            break;
        case 17:
            if (p) ch->globalSlide = p;
            break;
        case 20:
            if (p == 0) keyOff(ch);
            break;
        case 21:
            if (ch->instrument) ch->volEnvTick = ch->panEnvTick = p;
            break;
        case 25:
            if (p) ch->panSlide = p;
            break;
        case 27:
            if (p) ch->retrig = p;
            multiRetrig(ch, voice);
            break;
        case 29:
            if (p) ch->tremor = p;
            break;
        case 33:
            if (x == 1) {
                if (y) ch->xfinePortaUp = y;
                ch->period -= ch->xfinePortaUp;
            } else if (x == 2) {
                if (y) ch->xfinePortaDown = y;
                ch->period += ch->xfinePortaDown;
            }
            break;
    }
}

static void tickEffects(struct XMChannel* ch, unsigned voice) {
    uint8_t p = ch->cellParam, x = p >> 4, y = p & 15;
    uint8_t v = ch->cellVolume;
    switch (v >> 4) {
        case 6:
            volumeSlide(ch, v & 15);
            break;
        case 7:
            volumeSlide(ch, (v & 15) << 4);
            break;
        case 11:
            vibrato(ch);
            break;
        case 13: {
            int pan = ch->pan - (v & 15);
            ch->pan = pan < 0 ? 0 : pan;
            break;
        }
        case 14: {
            int pan = ch->pan + (v & 15);
            ch->pan = pan > 255 ? 255 : pan;
            break;
        }
        case 15:
            tonePortamento(ch);
            break;
    }
    switch (ch->cellEffect) {
        case 0:
            if (p == 0) break;
            switch (XM_Tick % 3) {
                case 1:
                    ch->outPeriod = arpeggioPeriod(ch, x);
                    break;
                case 2:
                    ch->outPeriod = arpeggioPeriod(ch, y);
                    break;
            }
            break;
        case 1:
            ch->period -= ch->portaUp * 4;
            if (ch->period < 1) ch->period = 1;
            break;
        case 2:
            ch->period += ch->portaDown * 4;
            if (ch->period > 32000) ch->period = 32000;
            break;
        case 3:
            tonePortamento(ch);
            break;
        case 4:
            vibrato(ch);
            break;
        case 5:
            tonePortamento(ch);
            volumeSlide(ch, ch->volSlide);
            break;
        case 6:
            vibrato(ch);
            volumeSlide(ch, ch->volSlide);
            break;
        case 7:
            tremolo(ch);
            break;
        case 10:
            volumeSlide(ch, ch->volSlide);
            break;
        case 14:
            switch (x) {
                case 9:
                    if (y && (XM_Tick % y) == 0) triggerNote(ch, voice, 0);
                    break;
                case 12:
                    if (XM_Tick == y) ch->volume = 0;
                    break;
            }
            break;
        case 17: {
            int g = XM_GlobalVolume;
            if (ch->globalSlide & 0xf0) {
                g += ch->globalSlide >> 4;
            } else {
                g -= ch->globalSlide & 15;
            }
            XM_GlobalVolume = g < 0 ? 0 : g > 64 ? 64 : g;
            break;
        }
        case 20:
            if (XM_Tick == p) keyOff(ch);
            break;
        case 25: {
            int pan = ch->pan;
            if (ch->panSlide & 0xf0) {
                pan += ch->panSlide >> 4;
            } else {
                pan -= ch->panSlide & 15;
            }
            ch->pan = pan < 0 ? 0 : pan > 255 ? 255 : pan;
            break;
        }
        case 27:
            multiRetrig(ch, voice);
            break;
        case 29: {
            unsigned on = (ch->tremor >> 4) + 1, off = (ch->tremor & 15) + 1;
            if ((ch->tremorPos % (on + off)) >= on) ch->outVolume = 0;
            ch->tremorPos++;
            break;
        }
    }
}

// Final volume, panning and pitch, once per tick.
static void updateVoice(struct XMChannel* ch, unsigned voice) {
    const struct XMInstrument* ins = ch->instrument;
    int32_t period = ch->outPeriod;
    unsigned envVol = 64, envPan = 32;
    if (ins) {
        if (ins->vol.flags & 1) {
            envVol = envelopeValue(ins->volPoints, &ins->vol, ch->volEnvTick);
            ch->volEnvTick = envelopeAdvance(ins->volPoints, &ins->vol, ch->volEnvTick, ch->keyOn);
            if (!ch->keyOn) {
                ch->fadeVolume -= ins->fadeout;
                if (ch->fadeVolume < 0) ch->fadeVolume = 0;
            }
        }
        if (ins->pan.flags & 1) {
            envPan = envelopeValue(ins->panPoints, &ins->pan, ch->panEnvTick);
            ch->panEnvTick = envelopeAdvance(ins->panPoints, &ins->pan, ch->panEnvTick, ch->keyOn);
        }
        if (ins->vibDepth && ins->vibRate) {
            // auto vibrato, ramped in over vibSweep ticks
            unsigned ramp = 256;
            if (ins->vibSweep) {
                if (ch->autoVibRamp < 256) ch->autoVibRamp += 256 / ins->vibSweep;
                ramp = ch->autoVibRamp > 256 ? 256 : ch->autoVibRamp;
            }
            uint8_t pos = ch->autoVibPos >> 2;
            int w = waveform(ins->vibType == 1 ? 2 : ins->vibType == 2 ? 1 : 0, pos);
            period += (w * ins->vibDepth * (int)ramp) >> 14;
            ch->autoVibPos += ins->vibRate;
        }
    }
    uint32_t vol = ch->outVolume * envVol * XM_GlobalVolume;  // up to 2^18
    vol = (vol >> 6) * (ch->fadeVolume >> 3) >> 12;           // up to 4096
    int pan = ch->pan;
    int dist = pan < 128 ? pan : 255 - pan;
    pan += ((int)envPan - 32) * (dist + (pan >= 128)) / 32;
    pan = pan < 0 ? 0 : pan > 255 ? 255 : pan;
    uint32_t left = (vol * s_panLaw[256 - pan]) >> 13;  // up to 0x4000
    uint32_t right = (vol * s_panLaw[pan]) >> 13;
    if (left > 0x3fff) left = 0x3fff;
    if (right > 0x3fff) right = 0x3fff;
    SPU_VOICES[voice].volumeLeft = (left * s_masterVolume) >> 16;
    SPU_VOICES[voice].volumeRight = (right * s_masterVolume) >> 16;
    if (ch->sample) SPU_VOICES[voice].sampleRate = periodToPitch(period, ch->sample->rateShift);
}

unsigned XM_Check(const void* song) {
    const struct XMSongHeader* h = (const struct XMSongHeader*)song;
    if (h->magic[0] != 'P' || h->magic[1] != 'S' || h->magic[2] != 'X' || h->magic[3] != 'M') return 0;
    if (h->version != 1 || h->channels == 0 || h->channels > 24 || h->songLength == 0) return 0;
    return h->channels;
}

unsigned XM_Load(const void* song, const uint8_t* samples) {
    initTables();
    SPUInit();
    XM_Channels = XM_Check(song);
    if (!XM_Channels) return 0;
    s_song = (const uint8_t*)song;
    s_header = (const struct XMSongHeader*)song;
    s_orders = s_song + s_header->ordersOffset;
    s_patterns = (const struct XMPatternEntry*)(s_song + s_header->patternsOffset);
    s_instruments = (const struct XMInstrument*)(s_song + s_header->instrumentsOffset);
    s_samples = (const struct XMSample*)(s_song + s_header->samplesOffset);
    XM_SongLength = s_header->songLength;
    if (samples && s_header->sampleBytes) SPUUpload(SPU_BASE, samples, s_header->sampleBytes);
    for (unsigned i = 0; i < 24; i++) SPUResetVoice(i);
    __builtin_memset(s_channels, 0, sizeof(s_channels));
    for (unsigned i = 0; i < XM_Channels; i++) {
        s_channels[i].pan = 128;
        s_channels[i].fadeVolume = 32768;
    }
    XM_Speed = s_header->speed;
    XM_Tick = 0;
    XM_GlobalVolume = 64;
    s_patternDelay = s_inDelay = 0;
    s_jumpOrder = s_breakRow = -1;
    s_loopRow = s_loopCount = 0;
    setBPM(s_header->bpm);
    setPosition(0, 0);
    SPU_CTRL = 0xc000;
    return XM_Channels;
}

void XM_Poll(void) {
    s_keyOn = 0;
    advanceHblanks();
    const int newRow = XM_Tick == 0 && !s_inDelay;
    if (newRow && !s_rowPointer) return;
    for (unsigned c = 0; c < XM_Channels; c++) {
        struct XMChannel* ch = &s_channels[c];
        if (newRow) {
            readCell(ch);
            unsigned delay = (ch->cellEffect == 14 && (ch->cellParam >> 4) == 13) ? (ch->cellParam & 15) : 0;
            if (!delay) startRow(ch, c);
            rowEffects(ch, c);
        } else {
            if (ch->cellEffect == 14 && ch->cellParam == (0xd0 | XM_Tick)) startRow(ch, c);
        }
        ch->outPeriod = ch->period;
        ch->outVolume = ch->volume;
        if (!newRow) tickEffects(ch, c);
        updateVoice(ch, c);
    }
    if (s_keyOn) {
        SPU_KEY_ON_LOW = s_keyOn;
        SPU_KEY_ON_HIGH = s_keyOn >> 16;
    }
    if (++XM_Tick < XM_Speed) return;
    XM_Tick = 0;
    if (s_patternDelay) {
        s_patternDelay--;
        s_inDelay = 1;
        return;
    }
    s_inDelay = 0;
    if (s_jumpOrder >= 0 || s_breakRow >= 0) {
        unsigned order = s_jumpOrder >= 0 ? (unsigned)s_jumpOrder : XM_CurrentOrder + 1;
        unsigned row = s_breakRow >= 0 ? (unsigned)s_breakRow : 0;
        if (order != XM_CurrentOrder) s_loopRow = 0;
        s_jumpOrder = s_breakRow = -1;
        setPosition(order, row);
    } else if (++XM_CurrentRow >= s_rows) {
        s_loopRow = 0;
        setPosition(XM_CurrentOrder + 1, 0);
    }
}

void XM_SetMusicVolume(uint32_t volume) { s_masterVolume = volume; }

void XM_PlaySoundEffect(unsigned voice, unsigned instrument, unsigned note, unsigned volume) {
    if (!s_header || voice >= 24 || !instrument || instrument > s_header->numInstruments) return;
    if (note < 1 || note > 96) return;
    uint8_t si = s_instruments[instrument - 1].keymap[note - 1];
    if (si >= s_header->numSamples) return;
    const struct XMSample* s = &s_samples[si];
    int realNote = (int)note - 1 + s->relnote;
    if (realNote < 0 || realNote >= 119 || !s->spuLength) return;
    if (volume > 64) volume = 64;
    SPU_VOICES[voice].volumeLeft = SPU_VOICES[voice].volumeRight = volume << 8;
    SPU_VOICES[voice].sampleRate = periodToPitch(notePeriod(realNote, s->finetune), s->rateShift);
    SPU_VOICES[voice].sampleStartAddr = (SPU_BASE + s->spuOffset + (s->pad / 28) * 16) >> 3;
    if (voice < 16) {
        SPU_KEY_ON_LOW = 1u << voice;
    } else {
        SPU_KEY_ON_HIGH = 1u << (voice - 16);
    }
}

void XM_Silence(void) {
    SPUInit();
    for (unsigned i = 0; i < 24; i++) SPUResetVoice(i);
}
