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


/* Full v1 monitor pipeline target: tty print, one PCDRV file, then an exit
   break with code 42. Loads at 0x80010000, below the RAM-linked monitor. */
#include "common/kernel/pcdrv.h"
#include "common/syscalls/syscalls.h"

int main(void) {
    volatile unsigned int *marker = (volatile unsigned int *)0x1f800000u;
    marker[0] = 0x71BE7E57u;
    syscall_puts("target: hello over the monitor tty\n");
    int fd = PCcreat("pipe-out.txt", 0);
    marker[1] = (unsigned int)fd;
    if (fd >= 0) {
        static const char msg[] = "written by the target through PCDRV\n";
        int n = PCwrite(fd, msg, sizeof(msg) - 1);
        marker[2] = (unsigned int)n;
        PCclose(fd);
    }
    syscall_puts("target: exiting with 42\n");
    register int a0 asm("a0") = 42;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    for (;;) {}
    return 0;
}
