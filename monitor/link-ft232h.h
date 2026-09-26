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

/* FT232H byte link, the FT232H EEPROM set to CPU-style FIFO (VCP on the host,
   which sees an ordinary serial port; the rate there is ignored). The chip is
   two byte registers: data with its A0 low, status with A0 high. Status bit 0
   = received data waiting, bit 1 = room to send. USB flow control holds the
   host back while the chip's buffers are full, so no receive window is needed.
   Anything emulating that interface (Pico-Dev) works the same way.

   Where the registers sit depends on the board, so both addresses are build
   knobs, KSEG1 addresses:
     psx232h, A0 on A20:  MONITOR_FT232H_DATA=0xbf000000 STATUS=0xbf100000,
                          EXP1 widened to 8 MB (the default below)
     Pico-Dev, USB:       0xbf000000 / 0xbf000001 (A1 high selects its UART
                          channel instead, 0xbf000002 / 0xbf000003)
   MONITOR_FT232H_EXP1_CONFIG, when defined, is written to the EXP1 delay/size
   register at init. Untested: no board here has one fitted. */

#define MONITOR_LINK_IS_STREAM 1

#ifndef MONITOR_FT232H_DATA
#define MONITOR_FT232H_DATA 0xbf000000
#define MONITOR_FT232H_STATUS 0xbf100000
#define MONITOR_FT232H_EXP1_CONFIG ((23 << 16) | 0x2422) /* 8 MB, psx232h's timing */
#endif

#define FT232H_DATA (*(volatile uint8_t *)(MONITOR_FT232H_DATA))
#define FT232H_STATUS (*(volatile uint8_t *)(MONITOR_FT232H_STATUS))
#define FT232H_RXF 0x01
#define FT232H_TXE 0x02

static inline void linkInit(void) {
#ifdef MONITOR_FT232H_EXP1_CONFIG
    *(volatile uint32_t *)0xbf801008 = MONITOR_FT232H_EXP1_CONFIG;
#endif
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
