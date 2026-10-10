/*

MIT License

Copyright (c) 2020 PCSX-Redux authors

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

#include "common/syscalls/syscalls.h"

// clang-format off

CESTER_BODY(
    static uint32_t s_heap[1024];
)

CESTER_TEST(userHeapReuse, test_instance,
    syscall_userInitheap(s_heap, sizeof(s_heap));
    void *a = syscall_userMalloc(256);
    void *b = syscall_userMalloc(256);
    cester_assert_not_null(a);
    cester_assert_not_null(b);
    cester_assert_ptr_not_equal(a, b);
    syscall_userFree(a);
    void *c = syscall_userMalloc(256);
    cester_assert_ptr_equal(a, c);
    syscall_userFree(b);
    syscall_userFree(c);
)

// WipEout (USA) initializes the user heap, writes over all of it with its own
// allocator, then hands pointers into it to free. The retail BIOS returns.
CESTER_TEST(userHeapFreeAfterOverwrite, test_instance,
    syscall_userInitheap(s_heap, sizeof(s_heap));
    for (unsigned i = 0; i < 1024; i++) s_heap[i] = 0;
    syscall_userFree(&s_heap[256]);
    syscall_userFree(&s_heap[512]);
    syscall_userMalloc(64);
    cester_assert_true(1);
)
