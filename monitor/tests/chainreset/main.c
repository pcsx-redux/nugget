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

/* Chain-reset survival: empties the kernel's priority-0 exception handler
   chain - the list the monitor's sysEnqIntRP entry lives on, reached from
   the table of tables at 0x100 the way the monitor reaches it - as a program
   resetting the kernel's chains would, then exits with `break 4, 0` and code
   0xc4a1. A monitor entered from the exception handler's patch slot still
   sees the break and the host reports exit code 0xc4a1; one on the chain
   alone never sees it. Prints nothing after the wipe: the kernel's syscall
   handler was on that chain too. */
#include <stdint.h>

#include "common/psxlibc/handlers.h"
#include "common/syscalls/syscalls.h"

#define CHAINRESET_EXIT 0xc4a1

static __attribute__((noreturn)) void exitWith(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    __builtin_unreachable();
}

int main(void) {
    struct HandlersStorage *chains = *(struct HandlersStorage **)0x100;
    ramsyscall_printf("chainreset: priority 0 chain at %p, head %p; wiping it\n", &chains[0], chains[0].first);
    chains[0].first = 0;
    exitWith(CHAINRESET_EXIT);
}
