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
   two register accumulators with the modulo deferred to the end. A computed
   value of exactly 0 is sent as 0xffffffff, so 0 never appears as a checksum.
   On ATCONS a received 0 means "not computed" and is accepted without
   verifying; on a byte link the checksum is mandatory and 0 fails. */
#define CKSUM_NONE 0u
static uint32_t s_txS1, s_txS2;
static uint32_t s_rxS1, s_rxS2;

/* Not inlined: the two callers would each carry the modulo sequences. */
static __attribute__((noinline)) uint32_t fletcherFinish(uint32_t s1, uint32_t s2) {
    uint32_t ck = ((s2 % 65535u) << 16) | (s1 % 65535u);
    return ck == CKSUM_NONE ? 0xffffffffu : ck;
}

void transportInit(void) { linkInit(); }

/* Running checksum for the streaming send API. The monitor sends exactly one
   frame at a time, so a single static accumulator is safe. */

void transportSendBegin(uint16_t type, uint16_t len) {
    s_txS1 = s_txS2 = 0;
#ifdef MONITOR_LINK_IS_STREAM
    linkPutByte(0); /* leaves console text, a frame follows */
#endif
    linkPutWord(FRAME_SYNC); /* SYNC is outside the checksum */
    transportSendWord(type);
    transportSendWord(len);
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


#ifdef MONITOR_LINK_IS_STREAM
/* Console input the monitor has no consumer for yet (card #676) is dropped and
   counted. */
static uint32_t s_consoleDropped;

void transportRecvBegin(uint16_t *type, uint16_t *len) {
    uint16_t t, l;
    linkRxOpen();
    uint8_t b = linkGetByte();
    for (;;) {
        if (b != 0) {
            s_consoleDropped++;
            b = linkGetByte();
            continue;
        }
        /* A run of 0s is one frame start. Each byte that fails to continue
           SYNC is looked at again as the start of what follows, so a 0 there
           begins the next frame. A LEN no frame can have is noise. */
        while ((b = linkGetByte()) == 0) {
        }
        if (b != (FRAME_SYNC & 0xff)) continue;
        b = linkGetByte();
        if (b != (FRAME_SYNC >> 8)) continue;
        t = linkGetWord();
        l = linkGetWord();
        if (l <= TRANSPORT_STREAM_MAX_LEN) break;
        b = linkGetByte();
    }
    s_rxS1 = s_rxS2 = 0;
    s_rxS1 += t; s_rxS2 += s_rxS1;
    s_rxS1 += l; s_rxS2 += s_rxS1;
    *type = t;
    *len = l;
}
#else
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
#endif

#ifdef MONITOR_LINK_IS_STREAM
#ifndef MONITOR_STOP_QUIET_SPINS
#define MONITOR_STOP_QUIET_SPINS 20000 /* ~7 ms on a retail PS1, ~80 byte times at 115200 */
#endif

int transportStopPending(void) {
    uint8_t b;
    if (!linkTryGetByte(&b)) return 0;
    int stop = 0;
    uint32_t quiet = 0;
    linkRxOpen();
    do {
        if (quiet == 0) {
            if (b == 0) {
                stop = 1;
            } else if (!stop) {
                s_consoleDropped++;
            }
        }
        quiet = linkTryGetByte(&b) ? 0 : quiet + 1;
    } while (quiet < MONITOR_STOP_QUIET_SPINS);
    linkRxClose();
#ifdef MONITOR_LINK_SIO1
    linkClearErrors(); /* the overrun a STOP frame longer than the FIFO leaves */
#endif
    return stop;
}
#else
int transportStopPending(void) { return 0; }
#endif

uint16_t transportRecvWord(void) {
    uint16_t w = linkGetWord();
    s_rxS1 += w; s_rxS2 += s_rxS1;
    return w;
}

int transportRecvEnd(void) {
    uint32_t rxck = linkGetWord();
    rxck |= ((uint32_t)linkGetWord()) << 16;
#ifdef MONITOR_LINK_IS_STREAM
    linkRxClose();
    /* The checksum is mandatory on a byte link: CKSUM_NONE fails below, since
       fletcherFinish never produces it. */
#else
    if (rxck == CKSUM_NONE) return TRANSPORT_OK; /* sender skipped it */
#endif
    return (rxck == fletcherFinish(s_rxS1, s_rxS2)) ? TRANSPORT_OK : TRANSPORT_ECKSUM;
}

#ifdef MONITOR_LINK_HAS_RATE
#ifndef MONITOR_RATE_WINDOW_SPINS
#define MONITOR_RATE_WINDOW_SPINS 3000000 /* ~1.08 s on a retail PS1: 2000000 measured 720 ms */
#endif

/* The frames accepted while trying a new rate: PING with no payload, then,
   once the host has seen the PONG, PING carrying the word 1. The host
   retries the first freely, so the second has to differ from it. */
static const uint8_t s_pingFrame[] = {0x00, 0xaa, 0x55, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00};
static const uint8_t s_confirmFrame[] = {0x00, 0xaa, 0x55, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x06, 0x00};

int transportHasRate(void) { return 1; }

/* Wait up to one window for the exact frame `f` at the current rate. */
static int awaitFrame(const uint8_t *f, unsigned n) {
    unsigned matched = 0;
    linkRxOpen();
    for (uint32_t spins = 0; spins < MONITOR_RATE_WINDOW_SPINS; spins++) {
        uint8_t b;
        if (!linkTryGetByte(&b)) continue;
        if (b == f[matched]) {
            if (++matched == n) {
                linkRxClose();
                return 1;
            }
        } else {
            matched = (b == f[0]) ? 1 : 0;
        }
    }
    linkRxClose();
    return 0;
}

int transportTryRate(uint16_t reload, uint16_t pongType, uint16_t pongWord) {
    uint16_t old = linkGetRate();
    linkSetRate(reload);
    /* The PING proves host -> PS1 at the new rate. Only a host that got the
       PONG sends the confirmation, which proves PS1 -> host. */
    if (awaitFrame(s_pingFrame, sizeof(s_pingFrame))) {
        transportSendFrame(pongType, &pongWord, 1);
        if (awaitFrame(s_confirmFrame, sizeof(s_confirmFrame))) return 1;
    }
    linkSetRate(old);
    return 0;
}
#else
int transportHasRate(void) { return 0; }
int transportTryRate(uint16_t reload, uint16_t pongType, uint16_t pongWord) { return -1; }
#endif

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
