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

#include "monitor/pcdrv.h"

#include "monitor/monitor.h"
#include "monitor/transport.h"

/* Bufferless: PCwrite streams the target's bytes straight out of its own buffer,
   and PCread decodes the reply straight into the target's own buffer. No staging
   RAM, no memcpy, no per-frame size cap (the target address is the buffer). */

/* op and all scalar fields are u32 = two words, low half first (section 13:
   PCDRV frames are 32-bit aligned). */
static void putU32(uint32_t v) {
    transportSendWord((uint16_t)(v & 0xffff));
    transportSendWord((uint16_t)(v >> 16));
}

static uint32_t getU32(void) {
    uint32_t lo = transportRecvWord();
    uint32_t hi = transportRecvWord();
    return lo | (hi << 16);
}

/* Byte length of a NUL-terminated PS1 string, counting the terminator. */
static uint32_t ps1StrLenZ(uint32_t ptr) {
    volatile const uint8_t *s = (volatile const uint8_t *)ptr;
    uint32_t n = 0;
    while (s[n]) n++;
    return n + 1; /* include the NUL */
}

/* Stream a NUL-terminated PS1 string as words, low-byte-first, zero-padded to a
   word boundary (section 2). lenZ counts the terminator. */
static void putCstr(uint32_t ptr, uint32_t lenZ) {
    volatile const uint8_t *s = (volatile const uint8_t *)ptr;
    uint32_t words = (lenZ + 1) >> 1;
    for (uint32_t w = 0; w < words; w++) {
        uint32_t i = 2 * w;
        uint8_t b0 = (i < lenZ) ? s[i] : 0;
        uint8_t b1 = (i + 1 < lenZ) ? s[i + 1] : 0;
        transportSendWord((uint16_t)(b0 | (b1 << 8)));
    }
}

/* Receive a scalar PCDRV_RESP [op:u32][ret:s32] and return ret. Drains any
   trailing words so the framing stays aligned; a malformed frame yields -1. */
static int32_t recvScalarResp(uint32_t op) {
    uint16_t type, len;
    transportRecvBegin(&type, &len);

    uint32_t echo = (len >= 2) ? getU32() : 0;
    int32_t ret = (len >= 4) ? (int32_t)getU32() : -1;
    for (uint16_t i = (len >= 4) ? 4 : (len >= 2 ? 2 : 0); i < len; i++) transportRecvWord();

    int rc = transportRecvEnd();
    if (rc != TRANSPORT_OK || type != MON_PCDRV_RESP || echo != op) return -1;
    return ret;
}

/* Bulk PCDRV payloads travel in frames of at most this many bytes, so every
   frame stays under the stream LEN cap (design section 13). */
#define PCDRV_CHUNK 8192

/* Take `words` payload words into dst[off..nbytes), dropping any padding past
   nbytes. Returns how many bytes landed. */
/* Counts every byte below nbytes as arrived, stores only those below cap. */
static uint32_t recvBytes(volatile uint8_t *dst, uint32_t off, uint32_t nbytes, uint32_t cap, uint16_t words) {
    uint32_t landed = 0;
    for (uint16_t i = 0; i < words; i++) {
        uint16_t w = transportRecvWord();
        uint32_t bi = off + (uint32_t)i * 2;
        if (bi < nbytes) landed++;
        if (bi + 1 < nbytes) landed++;
        if (bi < cap) dst[bi] = (uint8_t)(w & 0xff);
        if (bi + 1 < cap) dst[bi + 1] = (uint8_t)(w >> 8);
    }
    return landed;
}

static void putBytes(volatile const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i += 2) {
        uint8_t b0 = src[i];
        uint8_t b1 = (i + 1 < n) ? src[i + 1] : 0;
        transportSendWord((uint16_t)(b0 | (b1 << 8)));
    }
}

void monitorServicePcdrv(struct Registers *r, uint32_t op) {
    uint32_t a0 = r->GPR.n.a0;
    uint32_t a1 = r->GPR.n.a1;
    uint32_t a2 = r->GPR.n.a2;
    uint32_t a3 = r->GPR.n.a3;

    switch (op) {
        case PCDRV_INIT:
            transportSendBegin(MON_PCDRV_REQ, 2);
            putU32(op);
            transportSendEnd();
            r->GPR.n.v0 = (uint32_t)recvScalarResp(op);
            break;

        case PCDRV_CREAT:   /* fallthrough: same wire shape, different op */
        case PCDRV_OPEN: {
            /* REQ [op:u32][mode|flags:u32][name:cstr]; name = a0, mode/flags = a2. */
            uint32_t lenZ = ps1StrLenZ(a0);
            uint16_t cw = (uint16_t)((lenZ + 1) >> 1);
            transportSendBegin(MON_PCDRV_REQ, (uint16_t)(4 + cw));
            putU32(op);
            putU32(a2);
            putCstr(a0, lenZ);
            transportSendEnd();
            r->GPR.n.v0 = 0;
            r->GPR.n.v1 = (uint32_t)recvScalarResp(op);
            break;
        }

        case PCDRV_CLOSE:
            /* REQ [op:u32][fd:u32]; fd = a0. */
            transportSendBegin(MON_PCDRV_REQ, 4);
            putU32(op);
            putU32(a0);
            transportSendEnd();
            r->GPR.n.v0 = (uint32_t)recvScalarResp(op);
            break;

        case PCDRV_READ: {
            /* REQ [op:u32][fd:u32][len:u32]; fd = a1, len = a2, buf = a3.
               RESP [op:u32][ret:s32 = nbytes][bytes], at most PCDRV_CHUNK
               bytes, then continuation RESP [op:u32][bytes] frames until
               nbytes have arrived. Decode straight into the target's own
               buffer - no staging. */
            transportSendBegin(MON_PCDRV_REQ, 6);
            putU32(op);
            putU32(a1);
            putU32(a2);
            transportSendEnd();

            volatile uint8_t *dst = (volatile uint8_t *)a3;
            uint16_t type, len;
            transportRecvBegin(&type, &len);
            uint32_t echo = (len >= 2) ? getU32() : 0;
            int32_t ret = (len >= 4) ? (int32_t)getU32() : -1;
            uint32_t nbytes = (ret > 0) ? (uint32_t)ret : 0;
            uint16_t consumed = (len >= 4) ? 4 : (len >= 2 ? 2 : 0);
            uint32_t cap = nbytes < a2 ? nbytes : a2;
            uint32_t got = recvBytes(dst, 0, nbytes, cap, len - consumed);
            int ok = transportRecvEnd() == TRANSPORT_OK && type == MON_PCDRV_RESP && echo == op;
            while (ok && got < nbytes) {
                transportRecvBegin(&type, &len);
                echo = (len >= 2) ? getU32() : 0;
                got += recvBytes(dst, got, nbytes, cap, len >= 2 ? len - 2 : 0);
                ok = transportRecvEnd() == TRANSPORT_OK && type == MON_PCDRV_RESP && echo == op && len > 2;
            }
            r->GPR.n.v0 = 0;
            /* A host answering with more than was asked for is a protocol
               error; the excess was drained, not stored. */
            r->GPR.n.v1 = ok && nbytes <= a2 ? (uint32_t)ret : (uint32_t)-1;
            break;
        }

        case PCDRV_WRITE: {
            /* REQ [op:u32][fd:u32][len:u32][bytes], at most PCDRV_CHUNK bytes,
               then continuation REQ [op:u32][bytes] frames until len bytes
               have gone; fd = a1, len = a2, buf = a3. One RESP answers the
               whole write. Stream the target's bytes straight out of its
               buffer. */
            if ((int32_t)a2 < 0) {
                r->GPR.n.v0 = 0;
                r->GPR.n.v1 = (uint32_t)-1;
                break;
            }
            uint32_t total = a2;
            volatile const uint8_t *src = (volatile const uint8_t *)a3;
            uint32_t chunk = total < PCDRV_CHUNK ? total : PCDRV_CHUNK;
            transportSendBegin(MON_PCDRV_REQ, (uint16_t)(6 + ((chunk + 1) >> 1)));
            putU32(op);
            putU32(a1);
            putU32(total);
            putBytes(src, chunk);
            transportSendEnd();
            for (uint32_t off = chunk; off < total; off += chunk) {
                chunk = total - off < PCDRV_CHUNK ? total - off : PCDRV_CHUNK;
                transportSendBegin(MON_PCDRV_REQ, (uint16_t)(2 + ((chunk + 1) >> 1)));
                putU32(op);
                putBytes(src + off, chunk);
                transportSendEnd();
            }
            r->GPR.n.v0 = 0;
            r->GPR.n.v1 = (uint32_t)recvScalarResp(op);
            break;
        }

        case PCDRV_LSEEK:
            /* REQ [op:u32][fd:u32][offset:s32][whence:u32]; fd = a0, offset = a2,
               whence = a3. */
            transportSendBegin(MON_PCDRV_REQ, 8);
            putU32(op);
            putU32(a0);
            putU32(a2);
            putU32(a3);
            transportSendEnd();
            r->GPR.n.v0 = 0;
            r->GPR.n.v1 = (uint32_t)recvScalarResp(op);
            break;

        default:
            /* Unknown PCDRV code: leave a failure in v0 rather than hanging. */
            r->GPR.n.v0 = (uint32_t)-1;
            break;
    }
}
