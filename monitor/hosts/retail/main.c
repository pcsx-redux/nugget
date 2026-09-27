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


/* The monitor as a plain PS-EXE on top of the retail BIOS kernel, over SIO1,
   in two stages. core/ is the resident half, linked into the free RAM under
   the kernel's control blocks; this loader copies it there, hooks it into the
   kernel, and is never used again, so the target can have all of user RAM.
   The kernel state the monitor reads (0x60, 0x100) is placed there by the
   retail kernel itself; __globals and __globals60 are pinned to those
   addresses in the Makefiles. Interrupts go off for good first; a program
   the monitor runs gets its SR from RUN. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"
#include "monitor/install.h"
#include "monitor/link.h"

extern const uint32_t _binary_monitor_core_bin_start[];
extern const uint32_t _binary_monitor_core_bin_end[];
/* From core/monitor-core.elf. */
extern uint32_t __core_start[];

void installSio1Tty(void);
void drawLoaderSplash(void);

int main(void) {
    enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    drawLoaderSplash();

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
