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

#include "monitor/transport.h"

#include "monitor/link.h"

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

void transportInit(void) { linkInit(); }

/* Running checksum for the streaming send API. The monitor sends exactly one
   frame at a time, so a single static accumulator is safe. */

void transportSendBegin(uint16_t type, uint16_t len) {
    s_txS1 = s_txS2 = 0;
    linkPutWord(FRAME_SYNC); /* SYNC is outside the checksum */
    linkPutWord(type);
    s_txS1 += type; s_txS2 += s_txS1;
    linkPutWord(len);
    s_txS1 += len; s_txS2 += s_txS1;
}

void transportSendWord(uint16_t w) {
    linkPutWord(w);
    s_txS1 += w; s_txS2 += s_txS1;
}

void transportSendEnd(void) {
    uint32_t ck = fletcherFinish(s_txS1, s_txS2);
    linkPutWord((uint16_t)(ck & 0xffff));         /* low word first */
    linkPutWord((uint16_t)((ck >> 16) & 0xffff)); /* high word */
}

void transportSendFrame(uint16_t type, const uint16_t *payload, uint16_t len) {
    transportSendBegin(type, len);
    for (uint16_t i = 0; i < len; i++) transportSendWord(payload[i]);
    transportSendEnd();
}


void transportRecvBegin(uint16_t *type, uint16_t *len) {
    while (linkGetWord() != FRAME_SYNC) {
        /* resynchronize by hunting for the framing anchor */
    }
    s_rxS1 = s_rxS2 = 0;
    uint16_t t = linkGetWord();
    s_rxS1 += t; s_rxS2 += s_rxS1;
    uint16_t l = linkGetWord();
    s_rxS1 += l; s_rxS2 += s_rxS1;
    *type = t;
    *len = l;
}

uint16_t transportRecvWord(void) {
    uint16_t w = linkGetWord();
    s_rxS1 += w; s_rxS2 += s_rxS1;
    return w;
}

int transportRecvEnd(void) {
    uint32_t rxck = linkGetWord();
    rxck |= ((uint32_t)linkGetWord()) << 16;
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
