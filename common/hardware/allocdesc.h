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

/*

Shared layout for the generic allocator descriptor. Included by both guest code
(which builds a descriptor and registers it) and the emulator (which reads one
out of guest memory at registration time).

The tracker built on top of this never inspects guest memory to learn about a
block: every size and pointer comes from the intercepted call itself. That is
what lets it work against an allocator whose internal layout is unknown. The
only guest read is the one-time read of the descriptor below, which the guest
handed over explicitly.

Argument indices are register-relative in the MIPS O32 sense: 0..3 are a0..a3,
and 4 and up are stack arguments at sp + 0x10 + (index - 4) * 4. Use
PCSX_ALLOCARG_NONE for a role the entry does not have.

*/

#pragma once

#include <stdint.h>

/* Bare relative form on purpose: this header is included both from guest builds
   (include root src/mips/) and from the emulator (include root src/), and the
   "common/hardware/..." spelling used elsewhere in this directory only resolves
   for the first of those. */
#include "pcsxcmd.h"

#define PCSX_ALLOCARG_NONE 0xff

enum pcsx_AllocEntryKind {
    /* Terminator, and the value an uninitialized entry takes. */
    PCSX_ALLOC_KIND_END = 0,
    /* ptr = f(.., size, ..) -- malloc and friends, including operator new. */
    PCSX_ALLOC_KIND_ALLOC = 1,
    /* ptr = f(.., count, size, ..) -- the tracked size is count * size. */
    PCSX_ALLOC_KIND_CALLOC = 2,
    /* ptr = f(.., oldptr, size, ..) */
    PCSX_ALLOC_KIND_REALLOC = 3,
    /* f(.., ptr, ..) -- no return value is captured. */
    PCSX_ALLOC_KIND_FREE = 4,
    /* f(.., base, size, ..) -- resets the arena and sets its bounds. */
    PCSX_ALLOC_KIND_INIT = 5,
};

struct pcsx_AllocEntry {
    /* Address of the function's FIRST instruction. Not any address inside it:
       the tracker reads $ra and the incoming argument registers at this point,
       and the prologue is what destroys both. */
    uint32_t address;
    uint8_t kind;
    /* Index of the argument carrying a size. For CALLOC this is the element
       size; for INIT this is the arena length in bytes. */
    uint8_t argSize;
    /* Index of the argument carrying a pointer (FREE, REALLOC) or the arena
       base (INIT). */
    uint8_t argPtr;
    /* Index of the argument carrying an element count. CALLOC only. */
    uint8_t argCount;
};

struct pcsx_AllocDescriptor {
    /* op = PCSX_CMD_REGISTER_ALLOCATOR. Set size to the whole thing including
       the entry array, or the emulator cannot tell where the entries end. */
    struct pcsx_CommandHeader header;
    /* Guest pointer to a NUL-terminated label for this arena, or 0. Read once,
       at registration. */
    uint32_t nameAddr;
    /* Bounds of the arena. Both 0 means "unknown": the tracker will derive a
       display range from the allocations it observes, and will say so rather
       than pretending the gaps are free space. An INIT entry overrides these. */
    uint32_t heapStart;
    uint32_t heapEnd;
    uint32_t entryCount;
    struct pcsx_AllocEntry entries[];
};

/* Finding kinds, matching PCSX::AllocTracker::FindingKind. Index into
   pcsx_AllocReport::findingCounts. */
enum pcsx_AllocFinding {
    PCSX_ALLOCFINDING_DOUBLE_OR_UNKNOWN_FREE = 0,
    PCSX_ALLOCFINDING_INTERIOR_FREE = 1,
    PCSX_ALLOCFINDING_UNKNOWN_REALLOC = 2,
    PCSX_ALLOCFINDING_OVERLAP = 3,
    PCSX_ALLOCFINDING_FAILED_ALLOC = 4,
    PCSX_ALLOCFINDING_ZERO_SIZE = 5,

    PCSX_ALLOCFINDING_COUNT = 6
};

struct pcsx_AllocReport {
    /* op = PCSX_CMD_ALLOC_REPORT */
    struct pcsx_CommandHeader header;
    /* Input: which registered arena to report on, in registration order. */
    uint32_t arenaIndex;
    /* Output. */
    volatile uint32_t arenaCount;
    volatile uint32_t liveBlocks;
    volatile uint32_t liveBytes;
    volatile uint32_t peakBytes;
    volatile uint32_t allocCount;
    volatile uint32_t freeCount;
    volatile uint32_t pendingCaptures;
    volatile uint32_t findingCounts[PCSX_ALLOCFINDING_COUNT];
};

static __inline__ void pcsx_allocReportInit(struct pcsx_AllocReport* report, uint32_t arenaIndex) {
    pcsx_commandInit(&report->header, PCSX_CMD_ALLOC_REPORT, sizeof(struct pcsx_AllocReport));
    report->arenaIndex = arenaIndex;
}

/* Convenience for the common case: fill in the header for a descriptor holding
   `count` entries. The caller still fills nameAddr/heapStart/heapEnd/entries. */
static __inline__ void pcsx_allocDescriptorInit(struct pcsx_AllocDescriptor* descriptor, uint32_t count) {
    pcsx_commandInit(&descriptor->header, PCSX_CMD_REGISTER_ALLOCATOR,
                     sizeof(struct pcsx_AllocDescriptor) + count * sizeof(struct pcsx_AllocEntry));
    descriptor->entryCount = count;
}
