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

#include "monitor/lz4stream.h"

enum {
    S_TOKEN,
    S_LITLEN,
    S_LITERALS,
    S_OFFLO,
    S_OFFHI,
    S_MATCHLEN,
};

void lz4StreamInit(struct Lz4Stream *s, void *dest) {
    s->out = s->base = (uint8_t *)dest;
    s->state = S_TOKEN;
}

int lz4StreamFeed(struct Lz4Stream *s, uint8_t b) {
    /* The state lives in registers for the duration of the call and goes back
       to the struct once at the end. */
    uint8_t *out = s->out;
    uint32_t count = s->count;
    uint32_t offset = s->offset;
    uint32_t token = s->token;
    unsigned state = s->state;
    uint32_t match = 0; /* bytes to copy before returning, if any */
    int rc = LZ4S_OK;

    switch (state) {
        case S_TOKEN:
            token = b;
            count = b >> 4;
            if (count == 15) {
                state = S_LITLEN;
            } else {
                state = count ? S_LITERALS : S_OFFLO;
            }
            break;
        case S_LITLEN:
            count += b;
            if (b != 255) state = count ? S_LITERALS : S_OFFLO;
            break;
        case S_LITERALS:
            *out++ = b;
            if (--count == 0) state = S_OFFLO;
            break;
        case S_OFFLO:
            offset = b;
            state = S_OFFHI;
            break;
        case S_OFFHI:
            offset |= (uint32_t)b << 8;
#ifndef LZ4STREAM_TRUSTED
            if (offset == 0) rc = LZ4S_EBADOFFSET;
#endif
            match = (token & 15) + 4;
            state = (token & 15) == 15 ? S_MATCHLEN : S_TOKEN;
            break;
        case S_MATCHLEN:
            match = b;
            if (b != 255) state = S_TOKEN;
            break;
    }
    if (match && rc == LZ4S_OK) {
#ifndef LZ4STREAM_TRUSTED
        if ((uint32_t)(out - s->base) < offset) rc = LZ4S_EBADOFFSET;
#endif
        if (rc == LZ4S_OK) {
            const uint8_t *src = out - offset;
            while (match--) *out++ = *src++;
        }
    }
    s->out = out;
    s->count = count;
    s->offset = (uint16_t)offset;
    s->token = (uint8_t)token;
    s->state = (uint8_t)state;
    return rc;
}

int lz4StreamEndBlock(struct Lz4Stream *s) {
    /* A block ends after the literals of its last sequence. */
#ifndef LZ4STREAM_TRUSTED
    int ok = s->state == S_OFFLO || s->state == S_TOKEN;
    s->state = S_TOKEN;
    return ok ? LZ4S_OK : LZ4S_ETRUNCATED;
#else
    s->state = S_TOKEN;
    return LZ4S_OK;
#endif
}
