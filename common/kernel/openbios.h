/*

MIT License

Copyright (c) 2021 PCSX-Redux authors

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

#pragma once

#include <stdint.h>

// https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-18048.html
struct BuildId {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    uint8_t strings[];
};

static inline int isOpenBiosPresent() {
    uintptr_t* a0table = (uintptr_t*)0x200;
    return (a0table[11] & 3) == 1;
}

static inline uint32_t getOpenBiosApiVersion() {
    if (!isOpenBiosPresent()) return 0;
    register int n asm("t1") = 0x00;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    uintptr_t* a0table = (uintptr_t*)0x200;
    return ((uint32_t(*)())(a0table[11] ^ 1))();
}

static inline struct BuildId* getOpenBiosBuildId() {
    if (!isOpenBiosPresent()) return 0;
    register int n asm("t1") = 0x01;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    uintptr_t* a0table = (uintptr_t*)0x200;
    return ((struct BuildId * (*)())(a0table[11] ^ 1))();
}

/* OpenBIOS API 1. Each wrapper answers "no" (0, or nonzero for
   installOpenBiosExceptionSlot) on a retail BIOS and on OpenBIOS builds
   older than API 1, without calling anything. */

/* Calls OpenBIOS API entry `index` with two arguments. The call itself is
   in the asm so that nothing the compiler schedules between setting t1 and
   the jump can reuse t1; it clobbers what a normal call does. */
static inline uintptr_t openBiosCall(uint32_t index, uintptr_t arg0, uintptr_t arg1) {
    uintptr_t* a0table = (uintptr_t*)0x200;
    uintptr_t fn = a0table[11] ^ 1;
    register uintptr_t a0 asm("a0") = arg0;
    register uintptr_t a1 asm("a1") = arg1;
    register uintptr_t t1 asm("t1") = index;
    register uintptr_t v0 asm("v0");
    __asm__ volatile("jalr %4\n\tnop\n"
                     : "=r"(v0), "+r"(a0), "+r"(a1), "+r"(t1)
                     : "r"(fn)
                     : "$1", "v1", "a2", "a3", "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "ra", "hi", "lo",
                       "memory");
    return v0;
}

/* Nonzero if this OpenBIOS was built with the debug monitor in it
   (MONITOR=1): that monitor owns the exception path and its link from
   boot, and answers `break 4, 1` with HELLO. */
static inline int getOpenBiosMonitor() {
    if (getOpenBiosApiVersion() < 1) return 0;
    return (int)openBiosCall(0x02, 0, 0);
}

/* Puts a call to `fn` (`lui at / ori at, at / jalr at / nop`) into the
   kernel exception handler's patch slot `slot` (1..4) and flushes the
   i-cache, only if that slot is still four nops. `fn` runs with only at,
   v0, v1 and ra saved into the register frame k0 points at, and must
   return with k0 intact. Returns 0 when installed; nonzero, with nothing
   written, for a slot out of range or already taken. */
static inline int installOpenBiosExceptionSlot(int slot, void (*fn)(void)) {
    if (getOpenBiosApiVersion() < 1) return -1;
    return (int)openBiosCall(0x03, (uintptr_t)slot, (uintptr_t)fn);
}

/* A RAM range (KSEG0 address and size in bytes) that OpenBIOS guarantees
   it never uses, for resident code loaded under it. Returns 0 and a size
   of 0 when there is none to be had. */
static inline void* getOpenBiosCodeCave(uint32_t* size) {
    if (getOpenBiosApiVersion() < 1) {
        if (size) *size = 0;
        return 0;
    }
    return (void*)openBiosCall(0x04, (uintptr_t)size, 0);
}
