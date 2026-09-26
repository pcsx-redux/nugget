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

/* The physical link under the frame layer: either a blocking 16-bit word get
   and put, or a byte get and put for a stream link, plus a one-time init. Each backend is a header of static inline functions so
   the per-word calls inline into the framing loops; the build picks exactly
   one. */

#if defined(MONITOR_LINK_ATCONS)
#include "monitor/link-atcons.h"
#elif defined(MONITOR_LINK_SIO1)
#include "monitor/link-sio1.h"
#elif defined(MONITOR_LINK_FT232H)
#include "monitor/link-ft232h.h"
#else
#error "no monitor link selected: define MONITOR_LINK_ATCONS, MONITOR_LINK_SIO1 or MONITOR_LINK_FT232H"
#endif

/* A byte link (MONITOR_LINK_IS_STREAM) carries the DESIGN 2a stream: console
   bytes, with a 0 introducing a frame whose words go low byte first. It also
   provides linkRxOpen/linkRxClose around each receive, for flow control. */
#ifdef MONITOR_LINK_IS_STREAM
static inline uint16_t linkGetWord(void) {
    uint16_t lo = linkGetByte();
    return lo | ((uint16_t)linkGetByte() << 8);
}

static inline void linkPutWord(uint16_t w) {
    linkPutByte(w & 0xff);
    linkPutByte(w >> 8);
}
#endif
