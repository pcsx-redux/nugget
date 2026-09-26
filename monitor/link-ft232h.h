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

/* FT232H byte link on the expansion port, the FT232H EEPROM set to CPU-style
   FIFO (VCP on the host, which sees an ordinary serial port; the rate there
   is ignored). The chip is two bytes on EXP1: data with its A0 low, status
   with A0 high. Status bit 0 = received data waiting, bit 1 = room to send.
   USB flow control holds the host back while the FT232H's buffers are full,
   so no receive window is needed.

   The data register is at EXP1 + MONITOR_FT232H_DATA and the status register
   at EXP1 + MONITOR_FT232H_STATUS. The default puts A0 on A20, the wiring
   psx232h uses, which needs the EXP1 window widened to 8 MB. Untested: no
   board here has one fitted. */

#define MONITOR_LINK_IS_STREAM 1

#ifndef MONITOR_FT232H_DATA
#define MONITOR_FT232H_DATA 0x000000
#endif
#ifndef MONITOR_FT232H_STATUS
#define MONITOR_FT232H_STATUS 0x100000
#endif

#define FT232H_DATA (*(volatile uint8_t *)(0xbf000000 + MONITOR_FT232H_DATA))
#define FT232H_STATUS (*(volatile uint8_t *)(0xbf000000 + MONITOR_FT232H_STATUS))
#define FT232H_RXF 0x01
#define FT232H_TXE 0x02

static inline void linkInit(void) {
    /* EXP1: 8 MB window, 8-bit bus, psx232h's access timing. */
    *(volatile uint32_t *)0xbf801008 = (23 << 16) | 0x2422;
}

static inline void linkRxOpen(void) {}

static inline void linkRxClose(void) {}

static inline uint8_t linkGetByte(void) {
    while ((FT232H_STATUS & FT232H_RXF) == 0) {
    }
    return FT232H_DATA;
}

static inline void linkPutByte(uint8_t b) {
    while ((FT232H_STATUS & FT232H_TXE) == 0) {
    }
    FT232H_DATA = b;
}

static inline int linkTryGetByte(uint8_t *b) {
    if ((FT232H_STATUS & FT232H_RXF) == 0) return 0;
    *b = FT232H_DATA;
    return 1;
}
