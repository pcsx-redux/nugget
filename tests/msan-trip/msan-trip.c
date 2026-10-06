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

// Companion to the msan test for pcsx-redux#1928: SWL only initializes the bytes it
// writes, so an LWL consuming one more byte must still be reported. The emulator is
// expected to exit with the msan violation code (1) before main returns 0.

#include <stdint.h>

#include "common/hardware/pcsxhw.h"

// Out of line so the dynarec has $a0 written back when the emulator reads it.
static __attribute__((noipa)) void *msanAlloc(uint32_t size) { return pcsx_msanAlloc(size); }

int main() {
    pcsx_initMsan();
    uint8_t *p = msanAlloc(4);
    uint32_t v = 0x11223344;
    uint32_t r = 0;
    __asm__ volatile("swl %0, 1(%1)" : : "r"(v), "r"(p) : "memory");
    __asm__ volatile("lwl %0, 2(%1)" : "+r"(r) : "r"(p) : "memory");
    return 0;
}
