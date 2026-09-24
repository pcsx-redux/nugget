/*

MIT License

Copyright (c) 2022 PCSX-Redux authors

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

#include <stddef.h>

#include "common/libc/alloc.h"

#ifdef __cplusplus
extern "C" {
#endif

// psyqo's names for the common/libc allocator.
static inline void *psyqo_malloc(size_t size) { return libc_malloc(size); }
static inline void *psyqo_realloc(void *ptr, size_t size) { return libc_realloc(ptr, size); }
static inline void psyqo_free(void *ptr) { libc_free(ptr); }
static inline void *psyqo_heap_start() { return libc_heap_start(); }
static inline void *psyqo_heap_end() { return libc_heap_end(); }

#ifdef __cplusplus
}
#endif
