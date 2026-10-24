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

#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"
#include "xmplayer/xmplayer.h"

extern const uint8_t _binary_song_xmp_start[];
extern const uint8_t _binary_song_smp_start[];

#define printf ramsyscall_printf

static uint16_t s_nextCounter = 0;

static void checkMusic() {
    if (((int16_t)(s_nextCounter - COUNTERS[1].value)) <= 0) {
        XM_Poll();
        s_nextCounter += XM_hblanks;
    }
}

static void waitVSync() {
    int wasLocked = enterCriticalSection();
    uint32_t imask = IMASK;

    IMASK = imask | IRQ_VBLANK;

    while ((IREG & IRQ_VBLANK) == 0) checkMusic();
    IREG &= ~IRQ_VBLANK;
    IMASK = imask;
    if (!wasLocked) leaveCriticalSection();
}

void main() {
    // timer1 counts hblanks, which paces XM_Poll
    COUNTERS[1].mode = 0x0100;
    unsigned channels = XM_Load(_binary_song_xmp_start, _binary_song_smp_start);
    printf("XM: %d channels, %d orders\n", channels, XM_SongLength);
    unsigned row = 0xffffffff;
    unsigned order = 0xffffffff;
    s_nextCounter = COUNTERS[1].value + XM_hblanks;
    while (1) {
        if (row != XM_CurrentRow || order != XM_CurrentOrder) {
            row = XM_CurrentRow;
            order = XM_CurrentOrder;
            printf("XMROW %d %d %d\n", order, XM_CurrentPattern, row);
        }
        waitVSync();
    }
}
