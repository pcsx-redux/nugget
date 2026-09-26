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

/* The steps that hook the monitor into the kernel, shared between
   monitorMain() and hosts that run them from a separate init stage. */
#pragma once

#include <stdint.h>

#include "common/psxlibc/handlers.h"
#include "common/syscalls/syscalls.h"

/* The monitor's exception chain entry, defined in monitor.c. */
extern struct HandlerInfo s_monitorHandler;

/* Copy the installed 0x80 general-exception trampoline down to the 0x40 cop0
   break vector so a hardware breakpoint routes through the same handler and
   chain. Nothing is installed at 0x40 by OpenBIOS. */
static inline void monitorInstallCop0BreakVector(void) {
    uint32_t *v80 = (uint32_t *)0x80;
    uint32_t *v40 = (uint32_t *)0x40;
    for (int i = 0; i < 4; i++) v40[i] = v80[i];
    syscall_flushCache();
}

/* Own the break/fault path, priority 0, ahead of the syscall verifier. */
static inline void monitorHook(void) {
    syscall_sysEnqIntRP(0, &s_monitorHandler);
    monitorInstallCop0BreakVector();
}

/* Enter the monitor through the exception handler, so its loop runs there. */
static inline __attribute__((noreturn)) void monitorEnter(void) {
    __asm__ volatile("break 4, 1\n" : : : "memory");
    __builtin_unreachable();
}
