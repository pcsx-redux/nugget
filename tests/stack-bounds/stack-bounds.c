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

// The check must stay quiet on a program that keeps $sp inside its bounds:
// 32 levels of at least 64 bytes each against a 16KB window.

#include "stack-bounds.h"

int main() {
    if (!pcsx_commandSupported(PCSX_CMD_SET_STACK_BOUNDS)) {
        ramsyscall_printf("stack-bounds: operation not supported\n");
        return 1;
    }
    uint32_t sp = currentSP();
    if (!requireOk("set", pcsx_setStackBounds(sp - 16384, sp))) return 1;
    uint32_t result = recurse(32);
    if (!requireOk("clear", pcsx_clearStackBounds())) return 1;
    ramsyscall_printf("stack-bounds: PASS %u\n", result);
    return 0;
}
