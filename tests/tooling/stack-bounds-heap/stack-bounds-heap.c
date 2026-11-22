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

// The heap runs up to __stack_start, so once it is nearly full malloc hands out
// memory inside the stack bounds. With the bounds set and the allocator's
// metadata registered, the emulator is expected to exit with the stack bounds
// code (251) the next time $sp moves, before main returns 0. Allocation stops at
// the first block reaching into the bounds: the allocator writes a free block
// header past it, and the bounds are wide enough that this lands far below the
// live frames.

#include <stdlib.h>

#include "../stack-bounds/stack-bounds.h"

int main() {
    uint32_t sp = currentSP();
    uint32_t low = sp - 16384;
    if (!requireOk("set", pcsx_setStackBounds(low, sp))) return 1;
    unsigned blocks = 0;
    uint32_t end = 0;
    while (end <= low) {
        void* block = malloc(4096);
        if (!block) break;
        blocks++;
        end = (uint32_t)block + 4096;
    }
    uint32_t result = recurse(4);
    ramsyscall_printf("stack-bounds-heap: FAIL, %u blocks, last ends at 0x%08x over [0x%08x, 0x%08x], no trip (%u)\n",
                      blocks, end, low, sp, result);
    // Clear before returning: main's epilogue moves $sp above the bounds.
    pcsx_clearStackBounds();
    return 0;
}
