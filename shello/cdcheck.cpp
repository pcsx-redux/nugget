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

#include "shello/cdcheck.hh"

#include "psyqo/gpu.hh"

namespace shello {

void CDChecker::prepare(psyqo::GPU &gpu) {
    m_gpu = &gpu;
    m_cdrom.prepare();
}

void CDChecker::start() {
    m_coroutine = probe();
    m_coroutine.resume();
}

psyqo::Coroutine<> CDChecker::probe() {
    using namespace psyqo::timer_literals;

    // First pass shows "Checking"; once we've failed at least once the state is
    // sticky-Error across all retries, so the scene sees a clean
    // Checking -> Error -> Success progression rather than per-retry flicker.
    while (true) {
        bool ok = true;

        if (!co_await m_cdrom.reset()) {
            ok = false;
        } else if (!co_await m_isoParser.initialize()) {
            ok = false;
        } else {
            psyqo::ISO9660Parser::DirEntry entry;
            bool found = false;
            if (co_await m_isoParser.getDirentry("SYSTEM.CNF;1", &entry) &&
                (entry.type == psyqo::ISO9660Parser::DirEntry::FILE)) {
                found = true;
            } else if (co_await m_isoParser.getDirentry("PSX.EXE;1", &entry) &&
                       (entry.type == psyqo::ISO9660Parser::DirEntry::FILE)) {
                found = true;
            }
            if (!found) {
                ok = false;
            }
        }

        if (ok) {
            m_state = State::Success;
            co_return;
        }

        m_state = State::Error;
        co_await m_gpu->delay(1_s);
    }
}

}  // namespace shello
