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

#include "common/kernel/openbios.h"
#include "common/psxlibc/handlers.h"
#include "common/syscalls/syscalls.h"

/* The monitor's exception chain entry, defined in monitor.c. */
extern struct HandlerInfo s_monitorHandler;

/* The monitor's patch-slot entry and the slot it went into (0: none), both
   in monitor.c. */
void monitorSlotEntry(void);
extern uint32_t *s_monitorSlot;

/* Identifies the machine to the host in HELLO, defined in monitor.c. */
extern uint32_t s_biosChecksum;

/* Fletcher-32 over the 512 KiB at 0xBFC00000, the same sums the frames use
   (16-bit words low half first, 32-bit accumulators that wrap, each reduced
   mod 65535 at the end). One 32-bit load per word: the BIOS bus splits it
   into byte cycles itself, which beats four byte loads. */
static inline uint32_t monitorBiosChecksum(void) {
    const volatile uint32_t *p = (const volatile uint32_t *)0xbfc00000;
    uint32_t s1 = 0, s2 = 0;
    for (unsigned i = 0; i < 0x80000 / 4; i++) {
        uint32_t w = p[i];
        s1 += w & 0xffff;
        s2 += s1;
        s1 += w >> 16;
        s2 += s1;
    }
    return ((s2 % 65535u) << 16) | (s1 % 65535u);
}

/* Copy the installed 0x80 general-exception trampoline down to the 0x40 cop0
   break vector so a hardware breakpoint routes through the same handler and
   chain. Nothing is installed at 0x40 by OpenBIOS. */
static inline void monitorInstallCop0BreakVector(void) {
    uint32_t *v80 = (uint32_t *)0x80;
    uint32_t *v40 = (uint32_t *)0x40;
    for (int i = 0; i < 4; i++) v40[i] = v80[i];
    syscall_flushCache();
}

#if defined(OPENBIOS_H2X00_MONITOR) || defined(OPENBIOS_MONITOR)
/* Built into OpenBIOS: the slot is ours to name (kernel/vectors.s). */
extern uint32_t exceptionHandlerPatchSlot4[];

static inline uint32_t *monitorFindSlot(void) { return exceptionHandlerPatchSlot4; }
#else
/* B0 0x56, GetC0Table. */
static inline uint32_t **monitorGetC0Table(void) {
    register int n asm("t1") = 0x56;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((uint32_t * *(*)(void))0xb0)();
}

/* The kernel exception handler as the CPU reaches it: the 0x80 vector is
   `lui k0, hi / addiu k0, k0, lo / jr k0` on the retail kernel and on
   OpenBIOS alike. 0 if it is anything else. */
static inline uint32_t *monitorHandlerFromVector(void) {
    const uint32_t *v = (const uint32_t *)0x80;
    if ((v[0] >> 16) != 0x3c1a || (v[1] >> 16) != 0x275a || v[2] != 0x03400008) return 0;
    return (uint32_t *)((v[0] << 16) + (int16_t)v[1]);
}

/* The retail kernel's exception handler (C0 table entry 6) has four
   4-instruction patch slots at +0x70..+0xAF, between `sw v1, 0x80(k0)` at
   +0x6C and `sw a0, 0x10(k0)` at +0xB0; OpenBIOS's copy is laid out the
   same. Slot 1 is the memory card driver's, slot 2 the lightgun's; the
   monitor takes slot 4 (+0xA0), and only if the instructions around the
   slots are those and slot 4 is still four nops. Anything else: 0, and the
   monitor stays on the chain alone.

   On OpenBIOS, GetC0Table runs its patch matcher on the caller, which does
   not know the monitor's loader and halts the machine; there the handler
   comes from the 0x80 vector instead (the same place on OpenBIOS). */
static inline uint32_t *monitorFindSlot(void) {
    uint32_t *h = isOpenBiosPresent() ? monitorHandlerFromVector() : monitorGetC0Table()[6];
    uintptr_t a = (uintptr_t)h;
    if ((a & 3) || (a & 0x1fffffff) >= 0x00200000 - 0xb4) return 0; /* not in main RAM */
    if (h[0x6c / 4] != 0xaf430080 || h[0xb0 / 4] != 0xaf440010) return 0;
    uint32_t *slot = h + 0xa0 / 4;
    if (slot[0] | slot[1] | slot[2] | slot[3]) return 0;
    return slot;
}
#endif

/* `lui at, hi / ori at, at, lo / jalr at / nop`: a call to monitorSlotEntry
   from anywhere in the address space, clobbering at and ra only, both
   already saved when the slot runs. */
static inline void monitorPatchSlot(uint32_t *slot) {
    uint32_t e = (uint32_t)monitorSlotEntry;
    slot[0] = 0x3c010000 | (e >> 16);
    slot[1] = 0x34210000 | (e & 0xffff);
    slot[2] = 0x0020f809;
    slot[3] = 0;
    syscall_flushCache();
    s_monitorSlot = slot;
}

/* Own the break/fault path: at priority 0 on the kernel's chain, ahead of
   the syscall verifier, and from the exception handler's fourth patch slot
   ahead of every chain, so that a program resetting the chains keeps the
   monitor. The chain entry stays when the slot is taken (and is all there is
   when it is not); monitor.c keeps the two from handling one exception
   twice. MONITOR_NO_SLOT builds leave the handler alone. */
static inline void monitorHook(void) {
    syscall_sysEnqIntRP(0, &s_monitorHandler);
    monitorInstallCop0BreakVector();
#ifndef MONITOR_NO_SLOT
    uint32_t *slot = monitorFindSlot();
    if (slot) monitorPatchSlot(slot);
#endif
}

/* Enter the monitor through the exception handler, so its loop runs there. */
static inline __attribute__((noreturn)) void monitorEnter(void) {
    __asm__ volatile("break 4, 1\n" : : : "memory");
    __builtin_unreachable();
}
