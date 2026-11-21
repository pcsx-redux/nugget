/*

MIT License

Copyright (c) 2021 PCSX-Redux authors

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

#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"
#include "modplayer/modplayer.h"

extern const uint8_t _binary_timewarped_hit_start[];

#define printf ramsyscall_printf

static uint16_t s_nextCounter = 0;

// Function to call periodically if we can't use interrupts for some reason.
// If the code is running slower than the normal vsync speed, then you should
// call this during your frame computation code, in order to make sure you're
// not missing any tick.
static void checkMusic() {
    if (((int16_t)(s_nextCounter - COUNTERS[1].value)) <= 0) {
        MOD_Poll();
        s_nextCounter += MOD_hblanks;
    }
}

static void waitVSync() {
    int wasLocked = enterCriticalSection();
    uint32_t imask = IMASK;

    IMASK = imask | IRQ_VBLANK;

    while ((IREG & IRQ_VBLANK) == 0) {
        // Since our vsync is a kludge, we can't use the root counter IRQ
        // mechanism to call MOD_Poll, and so we have to poll here, during
        // vsync, for when our timer has passed the target value manually.

        // We *could* set up the timer properly using the target value system,
        // but this might make a few emulators sad. Beside, this is a perfectly
        // valid and common solution.
        checkMusic();
    }
    IREG &= ~IRQ_VBLANK;
    IMASK = imask;
    if (!wasLocked) leaveCriticalSection();
}

// VU meter: one segmented bar per channel. A key on jumps the bar to the
// channel's volume, volume changes move it, and it falls a fixed amount per
// frame. Levels are kept as volume * 16, so 63 is 1008.
#define VU_SEGMENTS 16
#define VU_SEGMENT_HEIGHT 8
#define VU_SEGMENT_PITCH 12
#define VU_BOTTOM 216
#define VU_FULL (63 << 4)
#define VU_DECAY 20
#define VU_MAX_BARS 20

static const union Color s_lit[3] = {
    {.r = 0, .g = 224, .b = 0}, {.r = 240, .g = 224, .b = 0}, {.r = 240, .g = 0, .b = 0}};
static const union Color s_dim[3] = {{.r = 0, .g = 40, .b = 0}, {.r = 48, .g = 44, .b = 0}, {.r = 48, .g = 0, .b = 0}};

static int16_t s_level[VU_MAX_BARS];
static uint8_t s_lastVolume[VU_MAX_BARS];
static uint8_t s_lastKeyOns[VU_MAX_BARS];

static void initDisplay() {
    struct DisplayModeConfig config = {.hResolution = HR_320,
                                       .vResolution = VR_240,
                                       .videoMode = VM_NTSC,
                                       .colorDepth = CD_15BITS,
                                       .videoInterlace = VI_OFF,
                                       .hResolutionExtended = HRE_NORMAL};
    sendGPUStatus(0x00000000);  // reset
    setDisplayArea(0, 0);
    setHorizontalRange(0x260, 0x260 + 320 * 8);
    setVerticalRange(16, 256);
    setDisplayMode(&config);
    struct FastFill ff = {.c = {.r = 0, .g = 0, .b = 0}, .x = 0, .y = 0, .w = 320, .h = 240};
    fastFill(&ff);
    waitGPU();
    enableDisplay();
}

static void drawVU() {
    unsigned channels = MOD_Channels;
    if (channels > VU_MAX_BARS) channels = VU_MAX_BARS;
    // Fast fills work on 16 pixel columns, so every x and width is a multiple of 16.
    int16_t pitch = (320 / channels) & ~15;
    int16_t width = pitch >= 48 ? 32 : 16;
    int16_t x = ((320 - channels * pitch) / 2 + (pitch - width) / 2) & ~15;
    for (unsigned ch = 0; ch < channels; ch++, x += pitch) {
        uint8_t volume = MOD_GetChannelVolume(ch);
        uint8_t keyOns = MOD_GetChannelKeyOns(ch);
        int16_t level = s_level[ch] - VU_DECAY;
        if (keyOns != s_lastKeyOns[ch]) {
            level = volume << 4;
        } else {
            level += (volume - s_lastVolume[ch]) << 4;
        }
        if (level < 0) level = 0;
        if (level > VU_FULL) level = VU_FULL;
        s_level[ch] = level;
        s_lastVolume[ch] = volume;
        s_lastKeyOns[ch] = keyOns;

        int lit = (level * VU_SEGMENTS + VU_FULL - 1) / VU_FULL;
        struct FastFill ff = {.x = x, .w = width, .h = VU_SEGMENT_HEIGHT};
        for (int seg = 0; seg < VU_SEGMENTS; seg++) {
            int band = seg < 10 ? 0 : seg < 13 ? 1 : 2;
            ff.c.packed = seg < lit ? s_lit[band].packed : s_dim[band].packed;
            ff.y = VU_BOTTOM - (seg + 1) * VU_SEGMENT_PITCH;
            fastFill(&ff);
        }
    }
}

void main() {
    printf("Loading MOD:\'%s\'\n", _binary_timewarped_hit_start);
    // We are going to use timer1 and its hblank counter to tell us when
    // we need to call MOD_Poll again. For this, we need timer1 to be
    // counting hblanks instead of the system clock.
    COUNTERS[1].mode = 0x0100;
    MOD_Load((struct MODFileFormat*)_binary_timewarped_hit_start);
    printf("%02d Channels, %02d Orders\n", MOD_Channels, MOD_SongLength);
    unsigned row = 0xffffffff;
    unsigned order = 0xffffffff;
    unsigned pattern = 0xffffffff;
    initDisplay();
    // Giving our initial counter a proper value.
    s_nextCounter = COUNTERS[1].value + MOD_hblanks;
    while (1) {
        if (row != MOD_CurrentRow || order != MOD_CurrentOrder || pattern != MOD_CurrentPattern) {
            row = MOD_CurrentRow;
            order = MOD_CurrentOrder;
            pattern = MOD_CurrentPattern;
            printf("Row: %02d, Order: %02d, Pattern: %02d\n", row, order, pattern);
        }
        waitVSync();
        drawVU();
    }
}
