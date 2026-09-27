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

#pragma once

#include <stdint.h>

/* Arcade watchdogs. The monitor decides when to kick; a board supplies the
   register. Any write to it clears the watchdog; the monitor writes 16 bits,
   as OpenBIOS's System 573 build does. A board with no watchdog gets no-ops.

   The 573's resets the board unless it is cleared every 350-400 ms. The
   monitor kicks it from the link's byte loops, which is where it waits
   while HALTED and where a long READ_MEM spends its time, and from interrupts
   while RUNNING. g_monitorKick gates the byte loops: the kernel tty writes
   through the same loop, and a target running with MON_FEAT_WATCHDOG clear
   must not be kicked by its own printf. */

#if defined(MONITOR_PLATFORM_SYS573)
#define MONITOR_WATCHDOG_ADDR 0x1f5c0000
#elif defined(MONITOR_PLATFORM_GV)
#define MONITOR_WATCHDOG_ADDR 0x1f780000
#endif

#ifdef MONITOR_WATCHDOG_ADDR
#define MONITOR_HAS_WATCHDOG 1
extern volatile uint8_t g_monitorKick;
static inline void monitorWatchdogKick(void) { *(volatile uint16_t *)MONITOR_WATCHDOG_ADDR = 0; }
static inline void monitorWatchdogIdle(void) {
    if (g_monitorKick) monitorWatchdogKick();
}
#else
static inline void monitorWatchdogKick(void) {}
static inline void monitorWatchdogIdle(void) {}
#endif
