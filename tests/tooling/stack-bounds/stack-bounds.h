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

// Shared by the stack-bounds tests, which each run as their own ps-exe since a
// tripped check ends the run.

#pragma once

#include <stdint.h>

#include "common/hardware/pcsxcmd.h"
#include "common/syscalls/syscalls.h"

static __inline__ uint32_t currentSP() {
    uint32_t sp;
    __asm__ volatile("move %0, $sp" : "=r"(sp));
    return sp;
}

// Each level holds a frame of at least 64 bytes. The array is volatile and the
// result is used after the call, so the recursion cannot become a loop.
static __attribute__((noinline)) uint32_t recurse(uint32_t depth) {
    volatile uint32_t frame[16];
    for (unsigned i = 0; i < 16; i++) frame[i] = depth + i;
    if (depth == 0) return frame[0];
    return recurse(depth - 1) + frame[15];
}

static __inline__ int requireOk(const char* what, uint32_t status) {
    if (status == PCSX_CMDSTATUS_OK) return 1;
    ramsyscall_printf("stack-bounds: %s returned status %d\n", what, status);
    return 0;
}
