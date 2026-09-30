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

namespace psyqo {
class GPU;
}

namespace shello {

// The shell's soundtrack is a tracker module (blip.hit) whose orders act as
// cues: order 2 is the idle/ping motif, order 3 the outro sting, order 4 the
// error sting. The original drove MOD_Poll off root counter 1 by hand; psyqo
// already owns counter 1 for its timer subsystem, so we drive the module
// through a periodic GPU timer instead, which is the idiomatic equivalent.
class Sound {
  public:
    void init(psyqo::GPU &gpu);
    void uninit(psyqo::GPU &gpu);

    // Order cues, mirroring spuSchedule* in the original shell.
    void schedulePing();
    void scheduleIdle();
    void scheduleOutro();
    void scheduleError();

    // True while the intro pattern is still playing.
    bool isIntro() const;

  private:
    unsigned m_timer = 0;
    bool m_hasTimer = false;
    int m_idle = 1;
};

}  // namespace shello
