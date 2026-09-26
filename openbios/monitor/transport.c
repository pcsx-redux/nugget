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

/* Frame checksum: Fletcher-32 over the 16-bit word stream (TYPE, LEN, payload),
   two register accumulators with the modulo deferred to the end. 0 on the wire
   means "not computed" and is accepted without verifying; a computed value of
   exactly 0 is sent as 0xffffffff so the sentinel stays unambiguous. */
#define CKSUM_NONE 0u
static uint32_t s_txS1, s_txS2;
static uint32_t s_rxS1, s_rxS2;

static uint32_t fletcherFinish(uint32_t s1, uint32_t s2) {
    uint32_t ck = ((s2 % 65535u) << 16) | (s1 % 65535u);
    return ck == CKSUM_NONE ? 0xffffffffu : ck;
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

void transportSendBegin(uint16_t type, uint16_t len) {
    s_txS1 = s_txS2 = 0;
    wordPut(FRAME_SYNC); /* SYNC is outside the checksum */
    wordPut(type);
    s_txS1 += type; s_txS2 += s_txS1;
    wordPut(len);
    s_txS1 += len; s_txS2 += s_txS1;
}

void transportSendWord(uint16_t w) {
    wordPut(w);
    s_txS1 += w; s_txS2 += s_txS1;
}

void transportSendEnd(void) {
    uint32_t ck = fletcherFinish(s_txS1, s_txS2);
    wordPut((uint16_t)(ck & 0xffff));         /* low word first */
    wordPut((uint16_t)((ck >> 16) & 0xffff)); /* high word */
}

void transportSendFrame(uint16_t type, const uint16_t *payload, uint16_t len) {
    transportSendBegin(type, len);
    for (uint16_t i = 0; i < len; i++) transportSendWord(payload[i]);
    transportSendEnd();
}


void transportRecvBegin(uint16_t *type, uint16_t *len) {
    while (wordGet() != FRAME_SYNC) {
        /* resynchronize by hunting for the framing anchor */
    }
    s_rxS1 = s_rxS2 = 0;
    uint16_t t = wordGet();
    s_rxS1 += t; s_rxS2 += s_rxS1;
    uint16_t l = wordGet();
    s_rxS1 += l; s_rxS2 += s_rxS1;
    *type = t;
    *len = l;
}

uint16_t transportRecvWord(void) {
    uint16_t w = wordGet();
    s_rxS1 += w; s_rxS2 += s_rxS1;
    return w;
}

int transportRecvEnd(void) {
    uint32_t rxck = wordGet();
    rxck |= ((uint32_t)wordGet()) << 16;
    if (rxck == CKSUM_NONE) return TRANSPORT_OK; /* sender skipped it */
    return (rxck == fletcherFinish(s_rxS1, s_rxS2)) ? TRANSPORT_OK : TRANSPORT_ECKSUM;
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
