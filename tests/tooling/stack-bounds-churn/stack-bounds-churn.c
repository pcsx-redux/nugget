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

// Setting the bounds over and over, the way a scheduler would on every context
// switch, must keep working and stay quiet. Each iteration alternates between
// two windows that both contain $sp and recurses inside the current one.

#include "../stack-bounds/stack-bounds.h"

int main() {
    uint32_t sp = currentSP();
    uint32_t total = 0;
    for (unsigned i = 0; i < 5000; i++) {
        uint32_t low = (i & 1) ? sp - 8192 : sp - 4096;
        uint32_t high = (i & 1) ? sp + 256 : sp;
        if (!requireOk("set", pcsx_setStackBounds(low, high))) return 1;
        total += recurse(4);
    }
    // Bad arguments are refused and leave the last good bounds in place.
    if (pcsx_setStackBounds(sp, sp - 4) != PCSX_CMDSTATUS_BAD_ARGUMENT) {
        ramsyscall_printf("stack-bounds-churn: low above high was accepted\n");
        return 1;
    }
    total += recurse(4);
    if (!requireOk("clear", pcsx_clearStackBounds())) return 1;
    ramsyscall_printf("stack-bounds-churn: PASS %u\n", total);
    return 0;
}
