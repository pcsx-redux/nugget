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

/* Layout of a scoped-profiler ring buffer, shared between the guest
   (psyqo/profiler.hh) and the emulator, which reads it live out of guest RAM
   once the guest registers it with PCSX_CMD_REGISTER_PROFILER (pcsxcmd.h).

   Pointer-sized fields are uint32_t so the layout is the same on a 64-bit
   host as on the R3000.

   Timestamps are (root counter 1 << 16) | root counter 2, with counter 1
   counting hblanks and counter 2 free-running on the system clock. Neither
   alone covers a frame, and they are not read atomically, so the elapsed
   cycles between two stamps are rebuilt from both: the line delta times the
   measured cycles per line gives an estimate within about one line, and the
   counter 2 delta gives the exact cycle count modulo 65536. That is exact
   for spans up to the counter 1 wrap, 65536 lines or about 4 seconds. See
   pcsx_profElapsed. */

#pragma once

#include <stdint.h>

#define PCSX_PROF_MAGIC 0x46505150 /* 'PQPF' */
#define PCSX_PROF_VERSION 1

enum pcsx_ProfFlags {
    /* Set when the console was in PAL mode at install time. Informational:
       elapsed times use cyclesPer16Lines, which was measured. */
    PCSX_PROF_FLAG_PAL = 1,
};

/* One static descriptor per profiled scope, in read-only data. Its address
   is the zone's identity. */
struct pcsx_ProfZone {
    uint32_t name; /* const char*, NUL-terminated */
    uint32_t file; /* const char*, NUL-terminated */
    uint32_t line;
};

/* One completed scope. Written when the scope ends, so nested scopes land
   innermost first. */
struct pcsx_ProfEntry {
    uint32_t zone; /* const struct pcsx_ProfZone* */
    uint32_t start;
    uint32_t end;
    /* Low 16 bits of the vsync counter when the scope started. */
    uint16_t vsync;
    /* Nesting level among profiled scopes, 0 for the outermost. */
    uint8_t depth;
    /* Shadow call stack depth when the scope started, in builds that keep
       one (psyqo's BUILD=FastDebug), clamped to 255. 0 otherwise. */
    uint8_t stackDepth;
};

struct pcsx_ProfHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t entrySize;
    /* Number of entries, a power of two. */
    uint32_t capacity;
    uint32_t flags;
    /* System clock cycles per 16 hblanks, measured at install time. */
    uint32_t cyclesPer16Lines;
    uint32_t entries; /* struct pcsx_ProfEntry*, capacity of them */
    /* Total entries ever written. Entry n lives at entries[n % capacity], and
       is complete once written has moved past it. A reader that falls more
       than capacity entries behind has lost the difference. */
    volatile uint32_t written;
};

/* What a reader checks before trusting a header: the layout version it
   knows, whole aligned entries, and an entry array that fits in 2 MB of RAM. */
static inline int pcsx_profHeaderValid(uint32_t magic, uint32_t version, uint32_t entrySize, uint32_t capacity,
                                       uint32_t entries) {
    return (magic == PCSX_PROF_MAGIC) && (version == PCSX_PROF_VERSION) &&
           (entrySize >= sizeof(struct pcsx_ProfEntry)) && !(entrySize & 3) && capacity &&
           !(capacity & (capacity - 1)) && (entrySize <= 64) && (capacity <= 0x200000 / entrySize) && !(entries & 3);
}

/* Elapsed system clock cycles from stamp a to stamp b, b no more than about
   4 seconds after a. */
static inline uint32_t pcsx_profElapsed(uint32_t a, uint32_t b, uint32_t cyclesPer16Lines) {
    uint32_t lines = (uint16_t)((b >> 16) - (a >> 16));
    uint32_t cycles = (uint16_t)(b - a);
    /* Fits: at most 65535 lines times about 34.7k cycles per 16 lines. */
    uint32_t estimate = (lines * cyclesPer16Lines) >> 4;
    int32_t wraps = ((int32_t)(estimate - cycles) + 32768) >> 16;
    return cycles + ((uint32_t)wraps << 16);
}
