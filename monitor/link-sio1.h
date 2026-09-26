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

#include "common/hardware/hwregs.h"
#include "common/hardware/sio.h"

/* SIO1 byte link at 115200 8N1 (x16, reload 18: 117600 baud, the setting the
   Unirom-carrying cables are known to run at).

   TX is gated by CTS in hardware. RX is not: the FIFO holds 8 bytes and the
   hardware never drops RTS by itself, so RTS is raised only while the frame
   layer is inside a receive (linkRxOpen .. linkRxClose) and polling. The host
   may still push a few bytes after RTS drops, which the FIFO absorbs. The
   sticky overrun bit is what says whether that held. */

#define MONITOR_LINK_IS_STREAM 1

#define SIO1_CTRL_BASE (SIO_CTRL_TXEN | SIO_CTRL_RXE)

/* Baud = 2073600 / reload at x16: 18 -> 115200 (117600 actual), 5 -> 414720,
   4 -> 518400. */
#ifndef MONITOR_SIO1_RELOAD
#define MONITOR_SIO1_RELOAD 18
#endif

static inline void linkInit(void) {
    SIOS[1].ctrl = SIO_CTRL_IR;
    SIOS[1].baudRate = MONITOR_SIO1_RELOAD;
    SIOS[1].mode = 0x4e; /* 1 stop bit, 8 bits, no parity, x16 */
    SIOS[1].ctrl = SIO1_CTRL_BASE;
}

static inline void linkRxOpen(void) { SIOS[1].ctrl = SIO1_CTRL_BASE | SIO_CTRL_RTS; }

static inline void linkRxClose(void) { SIOS[1].ctrl = SIO1_CTRL_BASE; }

static inline uint8_t linkGetByte(void) {
    while ((SIOS[1].stat & SIO_STAT_RXRDY) == 0) {
    }
    return SIOS[1].fifo;
}

static inline void linkPutByte(uint8_t b) {
    while ((SIOS[1].stat & SIO_STAT_TXRDY) == 0) {
    }
    SIOS[1].fifo = b;
}

/* Sticky receive overrun since the last linkClearErrors(). */
static inline int linkOverrun(void) { return (SIOS[1].stat & SIO_STAT_OE) != 0; }

static inline void linkClearErrors(void) { SIOS[1].ctrl |= SIO_CTRL_ERRRES; }

/* Line rate, for SET_BAUD (design section 2a). */
#define MONITOR_LINK_HAS_RATE 1

static inline uint16_t linkGetRate(void) { return SIOS[1].baudRate; }

/* Let the transmitter drain before changing the rate, so the ACK that
   precedes the switch leaves at the old one. */
static inline void linkSetRate(uint16_t reload) {
    while ((SIOS[1].stat & SIO_STAT_TXEMPTY) == 0) {
    }
    SIOS[1].baudRate = reload;
}

/* Non-blocking read: 1 and the byte if one was waiting, else 0. */
static inline int linkTryGetByte(uint8_t *b) {
    if ((SIOS[1].stat & SIO_STAT_RXRDY) == 0) return 0;
    *b = SIOS[1].fifo;
    return 1;
}
