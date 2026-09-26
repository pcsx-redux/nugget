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
#include "common/hardware/util.h"

/* ATCONS register file (0x1F8020xx), from hwregs.h; the 16-bit word channel
   lives one halfword past the byte FIFO.

   STAT bit map, confirmed by disassembling the DTL-H2000 debug stub's four
   transfer primitives (read/write byte, read/write word):
     bit0 (0x01) = RX word available  (host -> PS1, lhu 0x2004)
     bit2 (0x04) = TX word ready       (PS1 -> host, sh  0x2004)
     bit3 (0x08) = TX byte ready       (PS1 -> host, sb  0x2002)
     bit4 (0x10) = RX byte available   (host -> PS1, lbu 0x2002)
   The word channel is polled on STAT alone; unlike the byte channel it takes
   no per-word IRQ ack (the stub's word primitives are pure STAT-gated
   lhu/sh). */

#define STAT_RX_WORD 0x01
#define STAT_TX_WORD 0x04

static inline uint16_t linkGetWord(void) {
    while ((ATCONS_STAT & STAT_RX_WORD) == 0) {
        /* spin until the host has posted a word */
    }
    uint16_t w = ATCONS_WORD;
    flushWriteQueue();
    return w;
}

static inline void linkPutWord(uint16_t w) {
    while ((ATCONS_STAT & STAT_TX_WORD) == 0) {
        /* spin until the channel can accept a word */
    }
    ATCONS_WORD = w;
    flushWriteQueue();
}

static inline void linkInit(void) {
    /* Mirror dev_tty_init's ATCONS bring-up: clear the IRQ2 enable bit, then
       prime the IRQ / IRQ2 handshake registers. This is the shared ATCONS IRQ
       machinery; the word channel rides the same FPGA block as the byte TTY. */
    ATCONS_IRQ2 &= 0xfe;
    flushWriteQueue();
    ATCONS_IRQ = 0x20;
    ATCONS_IRQ2 |= 0x10;
    flushWriteQueue();
}
