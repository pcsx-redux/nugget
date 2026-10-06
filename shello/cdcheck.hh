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

#include "psyqo/cdrom-device.hh"
#include "psyqo/coroutine.hh"
#include "psyqo/iso9660-parser.hh"

namespace psyqo {
class GPU;
}

namespace shello {

// Replaces the original shell's hand-rolled CD-ROM state machine (cdrom.c).
// The original walked the controller protocol by hand each frame: reset, get
// TOC/ID, read the PVD, validate the "CD001" signature, read the root
// directory and scan it for PSX.EXE;1 / SYSTEM.CNF;1. psyqo's CDRomDevice and
// ISO9660Parser do all of that, so this is a coroutine that loops the same
// decision and publishes an observable state for the scene to react to.
//
// Note: like the original, audio-CD detection is not implemented (the original
// defined CD_SUCCESS_AUDIO but never reached it, so isCDAudio() was always
// false). isAudio() is kept as a faithful stub.
class CDChecker {
  public:
    enum class State {
        Checking,  // probe in progress, no verdict yet
        Error,     // no recognizable PlayStation disc (sticky across retries)
        Success,   // a bootable data disc was found
    };

    // Must be called from the Application's prepare() (after gpu().initialize()).
    void prepare(psyqo::GPU &gpu);
    // Kicks off the probe coroutine; call once from createScene().
    void start();

    State state() const { return m_state; }
    bool isError() const { return m_state == State::Error; }
    bool isSuccess() const { return m_state == State::Success; }
    bool isAudio() const { return false; }

  private:
    psyqo::Coroutine<> probe();

    psyqo::GPU *m_gpu = nullptr;
    psyqo::CDRomDevice m_cdrom;
    psyqo::ISO9660Parser m_isoParser = psyqo::ISO9660Parser(&m_cdrom);
    psyqo::Coroutine<> m_coroutine;
    State m_state = State::Checking;
};

}  // namespace shello
