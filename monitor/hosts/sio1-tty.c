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

static int ttyNull(void) { return 0; }

static int ttyAction(struct File *file, enum FileAction action) {
    if (action != PSXWRITE) return 0;
    const uint8_t *p = (const uint8_t *)file->buffer;
    int count = file->count;
    for (int i = 0; i < count; i++) {
        if (p[i]) linkPutByte(p[i]);
    }
    return count;
}

static const struct Device s_ttyDevice = {
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

static inline int removeDevice(const char *name) {
    register int n asm("t1") = 0x48;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int (*)(const char *))0xb0)(name);
}

void installSio1Tty(void) {
    syscall_close(0);
    syscall_close(1);
    removeDevice("tty");
    syscall_addDevice(&s_ttyDevice);
    syscall_open("tty00:", PSXF_READ);
    syscall_open("tty00:", PSXF_WRITE);
}
