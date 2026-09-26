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

/* A farm-shaped job for exercising a loader end to end: console text through
   the kernel's printf, PCDRV in both directions, and the exit break. Reads
   IN.TXT, writes it back upper-cased to OUT.TXT, and exits with the number of
   bytes read (up to 32 KB, so reads and writes span several frames), or
   0xbad on any PCDRV failure. */
#include <stdint.h>

#include "common/kernel/pcdrv.h"
#include "common/syscalls/syscalls.h"

static __attribute__((noreturn)) void exitWith(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    __builtin_unreachable();
}

static char s_buf[32768];

int main(void) {
    ramsyscall_printf("farmjob: start\n");
    if (PCinit() != 0) exitWith(0xbad);
    int in = PCopen("IN.TXT", 0, 0);
    if (in < 0) exitWith(0xbad);
    int n = PCread(in, s_buf, sizeof(s_buf));
    if (PCclose(in) != 0 || n < 0) exitWith(0xbad);
    for (int i = 0; i < n; i++) {
        if (s_buf[i] >= 'a' && s_buf[i] <= 'z') s_buf[i] -= 'a' - 'A';
    }
    int out = PCcreat("OUT.TXT", 0);
    if (out < 0) exitWith(0xbad);
    if (PCwrite(out, s_buf, n) != n) exitWith(0xbad);
    if (PCclose(out) != 0) exitWith(0xbad);
    ramsyscall_printf("farmjob: %d bytes\n", n);
    exitWith(n);
}
