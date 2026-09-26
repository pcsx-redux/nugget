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

static int copyMatch(struct Lz4Stream *s, uint32_t n) {
    uint8_t *out = s->out;
    if ((uint32_t)(out - s->base) < s->offset) return LZ4S_EBADOFFSET;
    const uint8_t *src = out - s->offset;
    while (n--) *out++ = *src++;
    s->out = out;
    return LZ4S_OK;
}

int lz4StreamFeed(struct Lz4Stream *s, uint8_t b) {
    switch (s->state) {
        case S_TOKEN:
            s->token = b;
            s->count = b >> 4;
            if (s->count == 15) {
                s->state = S_LITLEN;
            } else {
                s->state = s->count ? S_LITERALS : S_OFFLO;
            }
            return LZ4S_OK;
        case S_LITLEN:
            s->count += b;
            if (b != 255) s->state = s->count ? S_LITERALS : S_OFFLO;
            return LZ4S_OK;
        case S_LITERALS:
            *s->out++ = b;
            if (--s->count == 0) s->state = S_OFFLO;
            return LZ4S_OK;
        case S_OFFLO:
            s->offset = b;
            s->state = S_OFFHI;
            return LZ4S_OK;
        case S_OFFHI: {
            s->offset |= (uint16_t)b << 8;
            if (s->offset == 0) return LZ4S_EBADOFFSET;
            uint32_t n = (s->token & 15) + 4;
            s->state = (s->token & 15) == 15 ? S_MATCHLEN : S_TOKEN;
            return copyMatch(s, n);
        }
        case S_MATCHLEN:
            if (b != 255) s->state = S_TOKEN;
            return copyMatch(s, b);
    }
    return LZ4S_OK;
}

int lz4StreamEndBlock(struct Lz4Stream *s) {
    /* A block ends after the literals of its last sequence. */
    int ok = s->state == S_OFFLO || s->state == S_TOKEN;
    s->state = S_TOKEN;
    return ok ? LZ4S_OK : LZ4S_ETRUNCATED;
}
