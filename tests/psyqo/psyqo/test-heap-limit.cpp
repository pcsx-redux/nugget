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

#include <stdint.h>
#include "psyqo/alloc.h"

#include "snitch_all.hpp"

extern "C" char __stack_start;
extern "C" char __stack_reserve;

// Allocate until the heap runs out, then check no block reached into the
// stack reserve below __stack_start. The stack this test runs on lives there.
TEST_CASE("malloc keeps clear of the stack reserve", "[alloc]") {
    const uintptr_t reserve = reinterpret_cast<uintptr_t>(&__stack_reserve);
    const uintptr_t limit = reinterpret_cast<uintptr_t>(&__stack_start) - reserve;
    REQUIRE(reserve == 0x8000);

    static void* blocks[512];
    unsigned count = 0;
    uintptr_t highest = 0;
    for (size_t size = 64 * 1024; size >= 16; size /= 2) {
        while (count < 512) {
            void* p = psyqo_malloc(size);
            if (!p) break;
            blocks[count++] = p;
            uintptr_t end = reinterpret_cast<uintptr_t>(p) + size;
            if (end > highest) highest = end;
        }
    }
    for (unsigned i = 0; i < count; i++) psyqo_free(blocks[i]);

    CHECK(count > 0);
    CHECK(highest <= limit);
}
