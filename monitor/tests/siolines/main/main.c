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

/* SIO1 handshake-line probe: cycles CR through DTR/RTS off, RTS, DTR, both,
   about two seconds each, forever, so the host can watch which of its modem
   inputs follows which PS1 output. */
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/hardware/sio.h"

static void wait(void) {
    for (volatile uint32_t i = 0; i < 4000000; i++) {
    }
}

int main(void) {
    static const uint16_t states[] = {0, SIO_CTRL_RTS, SIO_CTRL_DTR, SIO_CTRL_RTS | SIO_CTRL_DTR};
    SIOS[1].ctrl = SIO_CTRL_IR;
    SIOS[1].baudRate = 18;
    SIOS[1].mode = 0x4e;
    for (;;) {
        for (int i = 0; i < 4; i++) {
            SIOS[1].ctrl = SIO_CTRL_TXEN | SIO_CTRL_RXE | states[i];
            while ((SIOS[1].stat & SIO_STAT_TXRDY) == 0) {
            }
            SIOS[1].fifo = '0' + i;
            wait();
        }
    }
    return 0;
}
