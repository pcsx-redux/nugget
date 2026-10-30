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

#include "psyqo/kernel.hh"
#include "snitch_all.hpp"

using namespace psyqo;

namespace {
char s_ran[8];
unsigned s_ranCount;
bool s_nested;
void ran(char c) {
    if (s_ranCount < sizeof(s_ran)) s_ran[s_ranCount] = c;
    s_ranCount++;
}
}  // namespace

// A queued callback that pumps the queue itself, the way a *Blocking call
// does, must not run its own entry again nor lose or repeat the others.
TEST_CASE("nested pumpCallbacks runs each callback once, in order") {
    s_ranCount = 0;
    s_nested = false;
    Kernel::queueCallback([]() {
        ran('A');
        if (s_nested) return;
        s_nested = true;
        Kernel::queueCallback([]() { ran('C'); });
        Kernel::Internal::pumpCallbacks();
    });
    Kernel::queueCallback([]() { ran('B'); });
    Kernel::Internal::pumpCallbacks();
    Kernel::Internal::pumpCallbacks();

    REQUIRE(s_ranCount == 3u);
    REQUIRE(s_ran[0] == 'A');
    REQUIRE(s_ran[1] == 'B');
    REQUIRE(s_ran[2] == 'C');
}
