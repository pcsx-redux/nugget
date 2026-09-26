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

/* A load-speed job: carries PAYLOAD (any file) in its own image, so the size
   and content of what the loader sends are chosen at build time, and exits
   with the 32-bit sum of those bytes so the host can check the load was
   exact. */

#include <stdint.h>

extern const uint8_t g_payload[], g_payloadEnd[];

static __attribute__((noreturn)) void exitWith(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    __builtin_unreachable();
}

int main(void) {
    uint32_t sum = 0;
    for (const uint8_t *p = g_payload; p < g_payloadEnd; p++) sum += *p;
    exitWith((int)sum);
}
