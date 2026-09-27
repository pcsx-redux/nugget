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

/* The monitor as a flash cart image on the retail BIOS, over SIO1. rom.s
   catches the boot as the BIOS starts loading the shell, copies this PS-EXE
   into RAM and jumps here, still inside the breakpoint exception. Put the
   kernel back the way a normally-loaded program finds it before the monitor
   takes over: no pending or enabled IRQs, the default exception return, and
   out of the critical section. Then install the resident half the same way
   the retail host does: ../retail/core, copied into the low-RAM cave. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"
#include "monitor/install.h"
#include "monitor/link.h"
#include "openbios/main/splash.h"

extern const uint32_t _binary_monitor_core_bin_start[];
extern const uint32_t _binary_monitor_core_bin_end[];
/* From ../retail/core/monitor-core.elf. */
extern uint32_t __core_start[];

void installSio1Tty(void);

int main(void) {
    IMASK = 0;
    IREG = 0;
    syscall_setDefaultExceptionJmpBuf();
    leaveCriticalSection();

    drawSplashScreen();

    const uint32_t *src = _binary_monitor_core_bin_start;
    uint32_t *dst = __core_start;
    while (src < _binary_monitor_core_bin_end) *dst++ = *src++;
    syscall_flushCache();

    s_biosChecksum = monitorBiosChecksum();
    linkInit();
    installSio1Tty();
    monitorHook();
    monitorEnter();
}
