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
#include "monitor/watchdog.h"

extern const uint32_t _binary_monitor_core_bin_start[];
extern const uint32_t _binary_monitor_core_bin_end[];
/* From core/monitor-core.elf. */
extern uint32_t __core_start[];
extern uint32_t __core_end[];

void installSio1Tty(void);
void releaseTty(void);
void drawLoaderSplash(void);

/* The first 64 KiB of RAM, the kernel's, through any segment (the retail
   kernel's own vector jumps to 0x00000C80). */
static int inKernelRam(uint32_t a) {
    uint32_t seg = a >> 29;
    return a && (seg == 0 || seg == 4 || seg == 5) && (a & 0x1fff0000) == 0;
}

/* Where the 0x80 vector jumps: `lui r / addiu r, r` or `lui r / ori r, r`,
   then `jr r`. 0 for anything else. */
static uint32_t vectorTarget(const volatile uint32_t *v) {
    uint32_t r = (v[0] >> 16) & 0x1f;
    if ((v[0] >> 21) != (0x0f << 5)) return 0; /* lui r */
    if (((v[1] >> 16) & 0x3ff) != ((r << 5) | r)) return 0;
    if ((v[2] & 0xfc1fffff) != 0x00000008 || ((v[2] >> 21) & 0x1f) != r) return 0;
    uint32_t op = v[1] >> 26;
    if (op == 0x09) return (v[0] << 16) + (uint32_t)(int32_t)(int16_t)v[1];
    if (op == 0x0d) return (v[0] << 16) | (v[1] & 0xffff);
    return 0;
}

int main(void) {
    /* Before touching anything: the resident monitor takes the break
       wherever it comes from, whatever the interrupt state. */
    if (getOpenBiosMonitor()) monitorEnter();

    enterCriticalSection();
    IMASK = 0;
    IREG = 0;

    /* DEV0: on the 573, crt0 has already set it (base 0x1f000000, timing
       0x24173f47) and cleared the watchdog once. The GV build leaves it as
       the kernel set it. */
    monitorWatchdogKick();

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
    struct KernelTable {
        uint32_t *data;
        uint32_t size;
    };
    struct KernelTable *const handlers = (struct KernelTable *)0x100;
    struct KernelTable *const events = (struct KernelTable *)0x120;
    syscall_flushCache();
    __builtin_memset(handlers->data, 0, handlers->size);
    __builtin_memset(events->data, 0, events->size);
    syscall_setDefaultExceptionJmpBuf();
    syscall_enqueueSyscallHandler(0);
    syscall_enqueueIrqHandler(3);
    syscall_enqueueRCntIrqs(1);

    drawLoaderSplash();

    /* A resident monitor (a cart's) may own the tty and slot 4 with code in
       the range the copy overwrites: let go of both while that code is
       still there. */
    releaseTty();
    monitorReleaseSlot();

    const uint32_t *src = _binary_monitor_core_bin_start;
    uint32_t *dst = __core_start;
    while (src < _binary_monitor_core_bin_end) *dst++ = *src++;
    syscall_flushCache();

    s_biosChecksum = monitorBiosChecksum();
    linkInit();
    installSio1Tty();

    /* The 0x80 vector as the launcher left it. On a 573, a launcher with its
       own exception handler (573in1) may leave the vector pointing there;
       point it back at the kernel's handler, taken from the kernel's own C0
       table (entry 6), when that is in the kernel's RAM. */
    volatile uint32_t *v80 = (volatile uint32_t *)0x80;
    uint32_t target = vectorTarget(v80);
#ifdef MONITOR_PLATFORM_SYS573
    if (!inKernelRam(target)) {
        uint32_t h = isOpenBiosPresent() ? 0 : (uint32_t)monitorGetC0Table()[6];
        if (!(h & 3) && inKernelRam(h)) {
            v80[0] = 0x3c1a0000 | ((h + 0x8000) >> 16); /* lui k0, hi */
            v80[1] = 0x275a0000 | (h & 0xffff);         /* addiu k0, k0, lo */
            v80[2] = 0x03400008;                        /* jr k0 */
            v80[3] = 0;
            syscall_flushCache();
            ramsyscall_printf("monitor: vector restored -> %08x\n", h);
        } else {
            ramsyscall_printf("monitor: vector foreign, not restored\n");
        }
    }
#endif

    if (monitorHook() == MONITOR_SLOT_OPENBIOS) ramsyscall_printf("monitor: slot 4 installed by OpenBIOS\n");
    monitorEnter();
}
