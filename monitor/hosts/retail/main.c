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
   the monitor runs gets its SR from RUN.

   On OpenBIOS (API 1 and up) built with the monitor, that monitor already
   owns the link and the exception path: the loader installs nothing and
   hands over to it with `break 4, 1`, which it answers with HELLO. On
   OpenBIOS without it, the core must fit the code cave OpenBIOS reserves,
   and OpenBIOS patches the exception handler's slot. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"
#include "monitor/install.h"
#include "monitor/link.h"

extern const uint32_t _binary_monitor_core_bin_start[];
extern const uint32_t _binary_monitor_core_bin_end[];
/* From core/monitor-core.elf. */
extern uint32_t __core_start[];
extern uint32_t __core_end[];

void installSio1Tty(void);
void drawLoaderSplash(void);

int main(void) {
    /* Before touching anything: the resident monitor takes the break
       wherever it comes from, whatever the interrupt state. */
    if (getOpenBiosMonitor()) monitorEnter();

    enterCriticalSection();
    uint32_t entryImask = IMASK;
    uint32_t entryIreg = IREG;
    uint32_t entrySioStat = HW_U32(0xbf801054);
    uint32_t entrySioMode = HW_U16(0xbf801058);
    uint32_t entrySioCtrl = HW_U16(0xbf80105a);
    uint32_t entrySioBaud = HW_U16(0xbf80105e);
    IMASK = 0;
    IREG = 0;

    /* Outside the cave the RAM is OpenBIOS's own, and the core only runs
       where it is linked: stop rather than write over the kernel. Say so on
       the kernel tty, and as console text on the link (the SIO1 tty device
       lives in the core, so not through printf). */
    if (!monitorCoreFitsCave(__core_start, __core_end)) {
        uint32_t size;
        void *cave = getOpenBiosCodeCave(&size);
        ramsyscall_printf("monitor: core %p..%p is outside the OpenBIOS code cave %p+%x, not installed\n",
                          __core_start, __core_end, cave, size);
        linkInit();
        for (const char *m = "monitor: core outside the OpenBIOS code cave, not installed\n"; *m; m++) linkPutByte(*m);
        for (;;);
    }

    /* A program started from the shell inherits whatever the shell left on
       the kernel's handler chains and event table. Reset them to the
       kernel's defaults, the way psyqo does when it keeps the kernel. */
    uint32_t chains[4];
    struct KernelTable {
        uint32_t *data;
        uint32_t size;
    };
    struct KernelTable *const handlers = (struct KernelTable *)0x100;
    struct KernelTable *const events = (struct KernelTable *)0x120;
    for (unsigned i = 0; i < 4; i++) chains[i] = handlers->data[i * 2];
    syscall_flushCache();
    __builtin_memset(handlers->data, 0, handlers->size);
    __builtin_memset(events->data, 0, events->size);
    syscall_setDefaultExceptionJmpBuf();
    syscall_enqueueSyscallHandler(0);
    syscall_enqueueIrqHandler(3);
    syscall_enqueueRCntIrqs(1);

    drawLoaderSplash();

    const uint32_t *src = _binary_monitor_core_bin_start;
    uint32_t *dst = __core_start;
    while (src < _binary_monitor_core_bin_end) *dst++ = *src++;
    syscall_flushCache();

    s_biosChecksum = monitorBiosChecksum();
    linkInit();
    installSio1Tty();
    ramsyscall_printf("monitor: entry imask %08x ireg %08x sio1 stat %08x mode %04x ctrl %04x baud %04x\n",
                      entryImask, entryIreg, entrySioStat, entrySioMode, entrySioCtrl, entrySioBaud);
    ramsyscall_printf("monitor: shell chains %08x %08x %08x %08x\n", chains[0], chains[1], chains[2], chains[3]);
    if (monitorHook() == MONITOR_SLOT_OPENBIOS) ramsyscall_printf("monitor: slot 4 installed by OpenBIOS\n");
    monitorEnter();
}
