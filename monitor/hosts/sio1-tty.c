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

/* Kernel TTY on SIO1 for the monitor's retail hosts, after Unirom's
   ttyredirect: replace the kernel's "tty" device with one whose writes go out
   the monitor's link as console text, and reopen stdin/stdout on it, so a
   target's printf reaches the host between frames. A 0x00 byte would enter a
   frame on a stream link, so it is dropped. Reads return nothing: SIO1 input
   belongs to the monitor. */
#include <stdint.h>

#include "common/psxlibc/device.h"
#include "common/psxlibc/stdio.h"
#include "common/syscalls/syscalls.h"
#include "monitor/link.h"

/* A host whose device lives in a separately linked resident stage builds this
   file a second time with SIO1_TTY_INSTALL_ONLY, for installSio1Tty alone. */
#ifdef SIO1_TTY_INSTALL_ONLY
extern const struct Device s_ttyDevice;
#else
static int ttyNull(void) { return 0; }

/* Console text is best effort. A host that closed the port leaves TX stalled
   for good (SIO1 CTS low, or an FT232H FIFO nobody reads), and a printf must
   not wedge the program then. So a byte that cannot leave within
   linkTxStallPolls() polls is dropped with the rest of its write, and later
   writes are dropped without waiting until the link takes a byte again. The
   frame path keeps linkPutByte's unbounded wait: a lost reply is worse than a
   late one. */
static int s_txStalled;

static int ttyAction(struct File *file, enum FileAction action) {
    if (action != PSXWRITE) return 0;
    const uint8_t *p = (const uint8_t *)file->buffer;
    int count = file->count;
    if (s_txStalled && !linkTxReady()) return count;
    s_txStalled = 0;
    for (const uint8_t *end = p + count; p < end; p++) {
        uint8_t b = *p;
        if (!b) continue;
        for (uint32_t n = linkTxStallPolls(); !linkTxReady();) {
            if (--n == 0) {
                s_txStalled = 1;
                return count;
            }
        }
        linkPutByte(b);
    }
    return count;
}

const struct Device s_ttyDevice = {
    .name = "tty",
    .flags = PSXDTTYPE_CHAR | PSXDTTYPE_CONS,
    .blockSize = 1,
    .desc = "MONITOR_SIO1",
    .init = (device_init)ttyNull,
    .open = (device_open)ttyNull,
    .action = ttyAction,
    .close = (device_close)ttyNull,
    .ioctl = (device_ioctl)ttyNull,
    .read = (device_read)ttyNull,
    .write = (device_write)ttyNull,
    .erase = (device_erase)ttyNull,
    .undelete = (void *)ttyNull,
    .firstFile = (device_firstFile)ttyNull,
    .nextFile = (device_nextFile)ttyNull,
    .format = (device_format)ttyNull,
    .chdir = (void *)ttyNull,
    .rename = (device_rename)ttyNull,
    .deinit = (device_deinit)ttyNull,
    .check = (void *)ttyNull,
};
#endif

static inline int removeDevice(const char *name) {
    register int n asm("t1") = 0x48;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int (*)(const char *))0xb0)(name);
}

/* Drop stdin/stdout and the "tty" device. The kernel calls the device's own
   close and deinit for that, so a loader replacing a resident monitor does
   this before its copy overwrites the old device's code. */
void releaseTty(void) {
    syscall_close(0);
    syscall_close(1);
    removeDevice("tty");
}

void installSio1Tty(void) {
    releaseTty();
    syscall_addDevice(&s_ttyDevice);
    syscall_open("tty00:", PSXF_READ);
    syscall_open("tty00:", PSXF_WRITE);
}
