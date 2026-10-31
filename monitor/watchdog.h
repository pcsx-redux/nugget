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
   must not be kicked by its own printf. It is MONITOR_KICK_HALTED while
   HALTED, and while RUNNING 1 or 0 from MON_FEAT_WATCHDOG. */
#define MONITOR_KICK_HALTED 2

#if defined(MONITOR_PLATFORM_SYS573)
#define MONITOR_WATCHDOG_ADDR 0x1f5c0000
#elif defined(MONITOR_PLATFORM_GV)
#define MONITOR_WATCHDOG_ADDR 0x1f780000
#endif

#ifdef MONITOR_WATCHDOG_ADDR
#define MONITOR_HAS_WATCHDOG 1
extern volatile uint8_t g_monitorKick;
static inline void monitorWatchdogKick(void) { *(volatile uint16_t *)MONITOR_WATCHDOG_ADDR = 0; }

/* MONITOR_WDT_KICK_HZ=N (default 0 = off) makes monitorWatchdogIdle kick at
   most N times a second, timed by root counter 2 at sysclk/8 (free-running,
   16 bits, no IRQ). The 16-bit deltas accumulate into a 32-bit tick count, so
   calls must come more often than every 15 ms. If the counter's mode is not
   what we set (a guest reconfigured it), it is set up again. Only while
   HALTED: the monitor saves the target's counter 2 on the way in and puts it
   back on resume (monitor.c), and while RUNNING the byte loops leave it
   alone, so the target's printf does not reprogram it. While RUNNING,
   interrupts kick only on VBlank (I_STAT bit 0). */
#ifndef MONITOR_WDT_KICK_HZ
#define MONITOR_WDT_KICK_HZ 0
#endif
#if MONITOR_WDT_KICK_HZ > 0
#define MONITOR_WDT_TICK_HZ 4233600u /* 33868800 / 8 */
#define MONITOR_WDT_TIMER_COUNT (*(volatile uint32_t *)0xbf801120)
#define MONITOR_WDT_TIMER_MODE (*(volatile uint32_t *)0xbf801124)
#define MONITOR_WDT_TIMER_TARGET (*(volatile uint32_t *)0xbf801128)
#define MONITOR_WDT_TIMER_MODE_VAL 0x200 /* free-running, sysclk/8 */
extern volatile uint32_t g_monitorKickElapsed;
extern volatile uint16_t g_monitorKickLast;
static inline void monitorWatchdogIdle(void) {
    if (g_monitorKick != MONITOR_KICK_HALTED) return;
    if ((MONITOR_WDT_TIMER_MODE & 0x3ff) != MONITOR_WDT_TIMER_MODE_VAL) {
        MONITOR_WDT_TIMER_MODE = MONITOR_WDT_TIMER_MODE_VAL;
        MONITOR_WDT_TIMER_TARGET = 0xffff;
        MONITOR_WDT_TIMER_COUNT = 0;
        g_monitorKickLast = 0;
        g_monitorKickElapsed = MONITOR_WDT_TICK_HZ; /* kick at once */
    }
    uint16_t now = (uint16_t)MONITOR_WDT_TIMER_COUNT;
    g_monitorKickElapsed += (uint16_t)(now - g_monitorKickLast);
    g_monitorKickLast = now;
    if (g_monitorKickElapsed < MONITOR_WDT_TICK_HZ / MONITOR_WDT_KICK_HZ) return;
    g_monitorKickElapsed = 0;
    monitorWatchdogKick();
}
static inline void monitorWatchdogIrq(void) {
    if (!g_monitorKick) return;
    if (*(volatile uint32_t *)0xbf801070 & 1) monitorWatchdogKick();
}
#else
static inline void monitorWatchdogIdle(void) {
    if (!g_monitorKick) return;
    monitorWatchdogKick();
}
static inline void monitorWatchdogIrq(void) { monitorWatchdogIdle(); }
#endif
#else
static inline void monitorWatchdogKick(void) {}
static inline void monitorWatchdogIdle(void) {}
static inline void monitorWatchdogIrq(void) {}
#endif
