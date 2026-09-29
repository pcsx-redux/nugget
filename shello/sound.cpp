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

#include "shello/sound.hh"

#include "psyqo/gpu.hh"

extern "C" {
#include "modplayer/modplayer.h"
}

// The blip.hit module, embedded into the binary via objcopy.
extern "C" const struct MODFileFormat _binary_blip_hit_start;

namespace shello {

void Sound::init(psyqo::GPU &gpu) {
    MOD_Load(&_binary_blip_hit_start);
    // Poll the module on its own cadence. MOD_hblanks is the number of HBlanks
    // between ticks; the module may change it, so we re-read it every poll.
    m_timer = gpu.armPeriodicTimer(MOD_hblanks * psyqo::GPU::US_PER_HBLANK, [this, &gpu](uint32_t) {
        MOD_Poll();
        // The original holds the music back by one row at order boundaries so
        // the cue transitions land cleanly. Preserve that quirk.
        if (MOD_CurrentOrder != 0) {
            if (MOD_CurrentRow == 60) {
                MOD_CurrentRow = 59;
            }
        }
        gpu.changeTimerPeriod(m_timer, MOD_hblanks * psyqo::GPU::US_PER_HBLANK);
    });
    m_hasTimer = true;
}

void Sound::uninit(psyqo::GPU &gpu) {
    if (m_hasTimer) {
        gpu.cancelTimer(m_timer);
        m_hasTimer = false;
    }
    MOD_Silence();
}

bool Sound::isIntro() const { return MOD_CurrentPattern == 0; }

void Sound::schedulePing() {
    if (!m_idle) return;
    if (MOD_CurrentOrder < 1) return;
    if ((MOD_CurrentOrder == 1) && (MOD_CurrentRow < 31)) return;

    MOD_ChangeOrderNextTick = 1;
    MOD_NextOrder = 2;
}

void Sound::scheduleIdle() {
    if (m_idle) return;
    m_idle = 1;
    MOD_ChangeOrderNextTick = 1;
    MOD_NextOrder = 2;
}

void Sound::scheduleOutro() {
    m_idle = 0;
    if (MOD_CurrentOrder < 1) return;
    if ((MOD_CurrentOrder == 1) && (MOD_CurrentRow < 31)) return;
    MOD_ChangeOrderNextTick = 1;
    MOD_NextOrder = 3;
}

void Sound::scheduleError() {
    m_idle = 0;
    MOD_ChangeOrderNextTick = 1;
    MOD_NextOrder = 4;
}

}  // namespace shello
