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

/* The loaders' splash: OpenBIOS's colour bars, drawn once per vblank over a
   few frames so both interlaced fields get them. Polls the VBLANK bit in
   I_STAT inside a critical section; no handler is installed. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"
#include "openbios/main/splash.h"

void drawLoaderSplash(void) {
    int wasLocked = enterCriticalSection();
    uint32_t imask = IMASK;
    IMASK = imask | IRQ_VBLANK;
    for (int i = 0; i < 4; i++) {
        IREG = ~IRQ_VBLANK;
        while ((IREG & IRQ_VBLANK) == 0);
        drawSplashScreen();
    }
    IREG = ~IRQ_VBLANK;
    IMASK = imask;
    if (!wasLocked) leaveCriticalSection();
}
