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

/*
 * The BIOS ROM's reset code writes 00070777h to the DEV8 Delay/Size register
 * (1F80101Ch), and memorycontrol.md says it is "usually 00070777h", but
 * programs read 00080777h back at entry. This writes a set of values and
 * reads each one back, to see whether the register stores what it is given.
 * The entry value is restored at the end.
 */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

#define DEV8 (*(volatile uint32_t *)0xbf80101c)

int main(void) {
    static const uint32_t vals[] = {0x00070777, 0x00080777, 0x00000777, 0x00010777};
    uint32_t entry = DEV8;
    ramsyscall_printf("D8RB entry=%08x\n", entry);
    for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        DEV8 = vals[i];
        uint32_t r1 = DEV8;
        uint32_t r2 = DEV8;
        DEV8 = entry;
        ramsyscall_printf("D8RB wrote=%08x read=%08x %08x\n", vals[i], r1, r2);
    }
    ramsyscall_printf("D8RB-DONE restored=%08x\n", DEV8);
    return 0;
}
