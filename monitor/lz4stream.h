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

/* LZ4 block decoder fed one input byte at a time. Output goes straight to its
   destination and earlier output is the match history, so consecutive blocks
   written to one buffer can reference each other (up to 64 KiB back). A match
   is copied as its length bytes arrive, so no single input byte produces more
   than 255 bytes of output. */

struct Lz4Stream {
    uint8_t *out;
    uint8_t *base;
    uint32_t count;
    uint16_t offset;
    uint8_t token;
    uint8_t state;
};

enum {
    LZ4S_OK = 0,
    LZ4S_EBADOFFSET = -1,
    LZ4S_ETRUNCATED = -2,
};

/* Start a stream writing at dest. Matches may not reach before dest. */
void lz4StreamInit(struct Lz4Stream *s, void *dest);
/* Feed one byte of the current block. */
int lz4StreamFeed(struct Lz4Stream *s, uint8_t b);
/* End the current block; the next byte fed starts a new one. Fails if the
   block stopped in the middle of a sequence. */
int lz4StreamEndBlock(struct Lz4Stream *s);
