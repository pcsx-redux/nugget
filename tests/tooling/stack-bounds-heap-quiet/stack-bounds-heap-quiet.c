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

// The heap check must stay quiet while every block the allocator hands out sits
// below the stack bounds: allocate, free, allocate again, and move $sp each time.

#include <stdlib.h>

#include "../stack-bounds/stack-bounds.h"

int main() {
    uint32_t sp = currentSP();
    if (!requireOk("set", pcsx_setStackBounds(sp - 16384, sp))) return 1;
    uint32_t result = 0;
    for (unsigned i = 0; i < 8; i++) {
        void* blocks[16];
        for (unsigned j = 0; j < 16; j++) blocks[j] = malloc(256 + 64 * j);
        result += recurse(8);
        for (unsigned j = 0; j < 16; j += 2) free(blocks[j]);
        result += recurse(8);
        for (unsigned j = 1; j < 16; j += 2) free(blocks[j]);
    }
    if (!requireOk("clear", pcsx_clearStackBounds())) return 1;
    ramsyscall_printf("stack-bounds-heap-quiet: PASS %u\n", result);
    return 0;
}
