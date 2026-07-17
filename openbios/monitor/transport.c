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

#include "openbios/monitor/transport.h"

#include "common/hardware/hwregs.h"
#include "common/hardware/util.h"

/* ATCONS register file (0x1F8020xx). STAT and the byte FIFO come from
   hwregs.h; the 16-bit word channel lives one halfword past the byte FIFO.

   STAT bit map, confirmed by disassembling the DTL-H2000 debug stub's four
   transfer primitives (read/write byte, read/write word):
     bit0 (0x01) = RX word available  (host -> PS1, lhu 0x2004)
     bit2 (0x04) = TX word ready       (PS1 -> host, sh  0x2004)
     bit3 (0x08) = TX byte ready       (PS1 -> host, sb  0x2002)
     bit4 (0x10) = RX byte available   (host -> PS1, lbu 0x2002)
   The word channel is polled on STAT alone; unlike the byte channel it takes
   no per-word IRQ ack (the stub's word primitives are pure STAT-gated
   lhu/sh). */
#define ATCONS_WORD HW_U16(0x1f802004)

#define STAT_RX_WORD 0x01
#define STAT_TX_WORD 0x04

static uint16_t wordGet(void) {
    while ((ATCONS_STAT & STAT_RX_WORD) == 0) {
        /* spin until the host has posted a word */
    }
    uint16_t w = ATCONS_WORD;
    flushWriteQueue();
    return w;
}

static void wordPut(uint16_t w) {
    while ((ATCONS_STAT & STAT_TX_WORD) == 0) {
        /* spin until the channel can accept a word */
    }
    ATCONS_WORD = w;
    flushWriteQueue();
}

/* Fold both bytes of a little-endian word into the running djb2 hash
   (h = h * 33 + byte, low byte first). */
static uint32_t djb2Word(uint32_t h, uint16_t w) {
    h = h * 33 + (w & 0xff);
    h = h * 33 + ((w >> 8) & 0xff);
    return h;
}

void transportInit(void) {
    /* Mirror dev_tty_init's ATCONS bring-up: clear the IRQ2 enable bit, then
       prime the IRQ / IRQ2 handshake registers. This is the shared ATCONS IRQ
       machinery; the word channel rides the same FPGA block as the byte TTY. */
    ATCONS_IRQ2 &= 0xfe;
    flushWriteQueue();
    ATCONS_IRQ = 0x20;
    ATCONS_IRQ2 |= 0x10;
    flushWriteQueue();
}

/* Running checksum for the streaming send API. The monitor sends exactly one
   frame at a time, so a single static accumulator is safe. */
static uint32_t s_txCksum;

void transportSendBegin(uint16_t type, uint16_t len) {
    s_txCksum = DJB2_SEED;
    wordPut(FRAME_SYNC); /* SYNC is outside the checksum */
    wordPut(type);
    s_txCksum = djb2Word(s_txCksum, type);
    wordPut(len);
    s_txCksum = djb2Word(s_txCksum, len);
}

void transportSendWord(uint16_t w) {
    wordPut(w);
    s_txCksum = djb2Word(s_txCksum, w);
}

void transportSendEnd(void) {
    wordPut((uint16_t)(s_txCksum & 0xffff));         /* low word first */
    wordPut((uint16_t)((s_txCksum >> 16) & 0xffff)); /* high word */
}

void transportSendFrame(uint16_t type, const uint16_t *payload, uint16_t len) {
    transportSendBegin(type, len);
    for (uint16_t i = 0; i < len; i++) transportSendWord(payload[i]);
    transportSendEnd();
}

/* Running checksum for the streaming receive API. */
static uint32_t s_rxCksum;

void transportRecvBegin(uint16_t *type, uint16_t *len) {
    while (wordGet() != FRAME_SYNC) {
        /* resynchronize by hunting for the framing anchor */
    }
    s_rxCksum = DJB2_SEED;
    uint16_t t = wordGet();
    s_rxCksum = djb2Word(s_rxCksum, t);
    uint16_t l = wordGet();
    s_rxCksum = djb2Word(s_rxCksum, l);
    *type = t;
    *len = l;
}

uint16_t transportRecvWord(void) {
    uint16_t w = wordGet();
    s_rxCksum = djb2Word(s_rxCksum, w);
    return w;
}

int transportRecvEnd(void) {
    uint32_t rxck = wordGet();
    rxck |= ((uint32_t)wordGet()) << 16;
    return (rxck == s_rxCksum) ? TRANSPORT_OK : TRANSPORT_ECKSUM;
}

int transportRecvFrame(uint16_t *type, uint16_t *payload, uint16_t maxLen, uint16_t *lenOut) {
    uint16_t t, len;
    transportRecvBegin(&t, &len);

    if (len > maxLen) {
        for (uint16_t i = 0; i < len; i++) transportRecvWord(); /* drain, stay aligned */
        transportRecvEnd();
        return TRANSPORT_EBADLEN;
    }

    for (uint16_t i = 0; i < len; i++) payload[i] = transportRecvWord();
    int rc = transportRecvEnd();
    if (rc != TRANSPORT_OK) return rc;

    *type = t;
    *lenOut = len;
    return TRANSPORT_OK;
}
