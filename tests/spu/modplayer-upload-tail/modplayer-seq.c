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

// The SPU side of modplayer/modplayer.c's loadInternal(), copied verbatim
// (SPUInit, SPUUploadInstruments, SPUResetVoice, SPUUnMute, and the
// assignments and memset between the upload and the unmute), compiled as C
// through common.mk exactly like modplayer.c is. The MOD_* globals are
// renamed T_* so this links next to nothing else.

#include <stddef.h>
#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/hardware/dma.h"
#include "common/hardware/spu.h"

struct SPUChannelData {
    uint16_t note;
    int16_t period;
    uint16_t slideTo;
    uint8_t slideSpeed;
    uint8_t volume;
    uint8_t sampleID;
    int8_t vibrato;
    uint8_t fx[4];
    uint16_t samplePos;
};

unsigned T_Channels = 4;
unsigned T_CurrentOrder;
unsigned T_CurrentPattern;
unsigned T_CurrentRow;
unsigned T_Speed;
unsigned T_Tick;
unsigned T_LoopStart;
unsigned T_LoopCount;
uint8_t T_PatternDelay;
const uint8_t* T_ModuleData;
const uint8_t* T_RowPointer;
int T_ChangeRowNextTick;
int T_ChangeOrderNextTick;

// Root counter 2 stamps for the timed variant.
uint16_t T_stampChcrIdle;
uint16_t T_stampCtrl;

static void SPUInit() {
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

static void SPUResetVoice(int voiceID) {
    SPU_VOICES[voiceID].volumeLeft = 0;
    SPU_VOICES[voiceID].volumeRight = 0;
    SPU_VOICES[voiceID].sampleRate = 0;
    SPU_VOICES[voiceID].sampleStartAddr = 0;
    SPU_VOICES[voiceID].adsrLo = 0x000f;
    SPU_VOICES[voiceID].currentVolume = 0;
    SPU_VOICES[voiceID].sampleRepeatAddr = 0;
    SPU_VOICES[voiceID].adsrHi = 0x0000;
}

static void SPUUploadInstruments(uint32_t SpuAddr, const uint8_t* data, uint32_t size) {
    uint32_t bcr = size >> 6;
    if (size & 0x3f) bcr++;
    bcr <<= 16;
    bcr |= 0x10;

    SPU_RAM_DTA = SpuAddr >> 3;
    SPU_CTRL = (SPU_CTRL & ~0x0030) | 0x0020;
    while ((SPU_CTRL & 0x0030) != 0x0020);
    // original code erroneously was doing SBUS_DEV4_CTRL = SBUS_DEV4_CTRL;
    SBUS_DEV4_CTRL &= ~0x0f000000;
    DMA_CTRL[DMA_SPU].MADR = (uint32_t)data;
    DMA_CTRL[DMA_SPU].BCR = bcr;
    DMA_CTRL[DMA_SPU].CHCR = 0x01000201;

    while ((DMA_CTRL[DMA_SPU].CHCR & 0x01000000) != 0);
}

// Same, plus one counter read after the CHCR wait.
static void SPUUploadInstrumentsTimed(uint32_t SpuAddr, const uint8_t* data, uint32_t size) {
    uint32_t bcr = size >> 6;
    if (size & 0x3f) bcr++;
    bcr <<= 16;
    bcr |= 0x10;

    SPU_RAM_DTA = SpuAddr >> 3;
    SPU_CTRL = (SPU_CTRL & ~0x0030) | 0x0020;
    while ((SPU_CTRL & 0x0030) != 0x0020);
    SBUS_DEV4_CTRL &= ~0x0f000000;
    DMA_CTRL[DMA_SPU].MADR = (uint32_t)data;
    DMA_CTRL[DMA_SPU].BCR = bcr;
    DMA_CTRL[DMA_SPU].CHCR = 0x01000201;

    while ((DMA_CTRL[DMA_SPU].CHCR & 0x01000000) != 0);
    T_stampChcrIdle = COUNTERS[2].value;
}

static void SPUUnMute() { SPU_CTRL = 0xc000; }

static struct SPUChannelData s_channelData[24];

const unsigned T_sizeofChannelData = sizeof(s_channelData);

// patternTable stands in for module->patternTable.
void T_modplayerLoad(const uint8_t* patternTable, uint32_t spuAddr, const uint8_t* sampleData, uint32_t size) {
    SPUInit();
    T_ModuleData = patternTable;
    SPUUploadInstruments(spuAddr, sampleData, size);

    T_CurrentOrder = 0;
    T_CurrentPattern = patternTable[0];
    T_CurrentRow = 0;
    T_Speed = 6;
    T_Tick = 6;
    T_RowPointer = T_ModuleData + 4 + 128 + T_CurrentPattern * T_Channels * 0x100;
    for (unsigned i = 0; i < 24; i++) SPUResetVoice(i);
    T_ChangeRowNextTick = 0;
    T_ChangeOrderNextTick = 0;
    T_LoopStart = 0;
    T_LoopCount = 0;

    T_PatternDelay = 0;
    __builtin_memset(s_channelData, 0, sizeof(s_channelData));

    SPUUnMute();
}

void T_modplayerLoadTimed(const uint8_t* patternTable, uint32_t spuAddr, const uint8_t* sampleData, uint32_t size) {
    SPUInit();
    T_ModuleData = patternTable;
    SPUUploadInstrumentsTimed(spuAddr, sampleData, size);

    T_CurrentOrder = 0;
    T_CurrentPattern = patternTable[0];
    T_CurrentRow = 0;
    T_Speed = 6;
    T_Tick = 6;
    T_RowPointer = T_ModuleData + 4 + 128 + T_CurrentPattern * T_Channels * 0x100;
    for (unsigned i = 0; i < 24; i++) SPUResetVoice(i);
    T_ChangeRowNextTick = 0;
    T_ChangeOrderNextTick = 0;
    T_LoopStart = 0;
    T_LoopCount = 0;

    T_PatternDelay = 0;
    __builtin_memset(s_channelData, 0, sizeof(s_channelData));

    T_stampCtrl = COUNTERS[2].value;
    SPUUnMute();
}

// The control: the same upload, unmute immediately.
void T_controlLoad(uint32_t spuAddr, const uint8_t* sampleData, uint32_t size) {
    SPUInit();
    SPUUploadInstrumentsTimed(spuAddr, sampleData, size);
    T_stampCtrl = COUNTERS[2].value;
    SPUUnMute();
}

// Reference: the same upload, a long spin, then unmute. Must not lose bytes.
void T_referenceLoad(uint32_t spuAddr, const uint8_t* sampleData, uint32_t size) {
    SPUInit();
    SPUUploadInstruments(spuAddr, sampleData, size);
    for (volatile unsigned i = 0; i < 20000; i = i + 1);
    SPUUnMute();
}
