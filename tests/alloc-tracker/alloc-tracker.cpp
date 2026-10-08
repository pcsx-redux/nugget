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

Points the generic allocator tracker at PSYQo's own allocator and checks what
it concluded against what this program actually did.

The point is not that PSYQo needs tracking - it has a dedicated fast path that
walks its free list directly. The point is that PSYQo is the one allocator
where ground truth is available two independent ways, so it is the only thing
that can tell us the tracker's state machine is correct rather than merely
plausible. Everything is checked as a DELTA around each operation, so an
allocation from somewhere else in the runtime shifts the baseline instead of
failing the test.

Run: pcsx-redux -no-ui -run -testmode -interpreter -debugger \
       -bios src/mips/openbios/openbios.bin -loadexe alloc-tracker.ps-exe
Exit code 0 means every check passed. Any other value is the number of the
check that failed, so a failure names itself without needing the log.

The -debugger flag is not optional and neither is -interpreter: execution
breakpoints do not exist under the dynamic recompiler, and -testmode skips
pcsx.json entirely so the compiled-in default (dynarec on, debugger off) is
what you get otherwise.

*/

#include <stdint.h>

#include "common/hardware/allocdesc.h"
#include "common/hardware/pcsxcmd.h"
#include "common/hardware/pcsxhw.h"
#include "psyqo/alloc.h"

namespace {

// pcsx_putc output is assembled host-side and only flushed on '\n'. A line
// without one is never printed at all, so every line here ends in one.
void print(const char* s) {
    while (*s) pcsx_putc(*s++);
}

void printU(uint32_t value) {
    char buffer[12];
    int i = 0;
    if (value == 0) {
        pcsx_putc('0');
        return;
    }
    while (value) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i) pcsx_putc(buffer[--i]);
}

void report(const char* label, uint32_t got, uint32_t want) {
    print("  ");
    print(label);
    print(": got ");
    printU(got);
    print(" want ");
    printU(want);
    print("\n");
}

int g_check = 0;

// Every check is numbered so a nonzero exit says which one broke.
bool expect(const char* label, uint32_t got, uint32_t want) {
    g_check++;
    if (got == want) return true;
    print("FAIL check ");
    printU(g_check);
    print("\n");
    report(label, got, want);
    return false;
}

struct pcsx_AllocReport g_report;

bool fetch(uint32_t arenaIndex) {
    pcsx_allocReportInit(&g_report, arenaIndex);
    pcsx_command(&g_report);
    return g_report.header.results.status == PCSX_CMDSTATUS_OK;
}

// Three entries: malloc(size), free(ptr), realloc(ptr, size). PSYQo has no
// calloc, so there is nothing to register for it.
//
// A union rather than a struct with a trailing array: pcsx_AllocDescriptor ends
// in a flexible array member, and a flexible array member may not be followed
// by another field. The union gives the allocation its real size without ever
// putting anything after `entries`.
constexpr unsigned c_entryCount = 3;
union DescriptorStorage {
    struct pcsx_AllocDescriptor descriptor;
    uint8_t raw[sizeof(struct pcsx_AllocDescriptor) + c_entryCount * sizeof(struct pcsx_AllocEntry)];
};

DescriptorStorage g_storage;
const char g_name[] = "psyqo";

}  // namespace

int main() {
    print("alloc-tracker: start\n");

    // 1. Feature detection must work before anything else is believable. On an
    // emulator without the command port the poison survives and this is false.
    struct pcsx_QueryResult query;
    pcsx_commandInit(&query.header, PCSX_CMD_QUERY, sizeof(query));
    query.protocolVersion = 0;
    pcsx_command(&query);
    if (!expect("query status", query.header.results.status, PCSX_CMDSTATUS_OK)) return g_check;
    if (!expect("protocol version", query.protocolVersion, PCSX_CMD_PROTOCOL_VERSION)) return g_check;
    if (!expect("register op supported", pcsx_commandSupported(PCSX_CMD_REGISTER_ALLOCATOR), 1)) return g_check;
    if (!expect("report op supported", pcsx_commandSupported(PCSX_CMD_ALLOC_REPORT), 1)) return g_check;
    // An operation this build cannot possibly implement must come back as a
    // clean "no", not as silence and not as a wrong yes.
    if (!expect("bogus op unsupported", pcsx_commandSupported(100), 0)) return g_check;

    // 2. Force the heap to initialize before registering, so the tracker never
    // sees the allocator's own lazy first-call bookkeeping.
    void* warmup = psyqo_malloc(16);
    psyqo_free(warmup);

    pcsx_allocDescriptorInit(&g_storage.descriptor, c_entryCount);
    g_storage.descriptor.nameAddr = (uint32_t)g_name;
    g_storage.descriptor.heapStart = (uint32_t)psyqo_heap_start();
    g_storage.descriptor.heapEnd = (uint32_t)psyqo_heap_end();
    // The libc_ names, not the psyqo_ ones: psyqo/alloc.h spells those as static
    // inline forwarders, so taking their address names an out-of-line copy that
    // no call ever reaches, and the breakpoints would never fire.
    g_storage.descriptor.entries[0].address = (uint32_t)&libc_malloc;
    g_storage.descriptor.entries[0].kind = PCSX_ALLOC_KIND_ALLOC;
    g_storage.descriptor.entries[0].argSize = 0;
    g_storage.descriptor.entries[0].argPtr = PCSX_ALLOCARG_NONE;
    g_storage.descriptor.entries[0].argCount = PCSX_ALLOCARG_NONE;
    g_storage.descriptor.entries[1].address = (uint32_t)&libc_free;
    g_storage.descriptor.entries[1].kind = PCSX_ALLOC_KIND_FREE;
    g_storage.descriptor.entries[1].argSize = PCSX_ALLOCARG_NONE;
    g_storage.descriptor.entries[1].argPtr = 0;
    g_storage.descriptor.entries[1].argCount = PCSX_ALLOCARG_NONE;
    // libc_realloc(ptr, size): pointer first, size second.
    g_storage.descriptor.entries[2].address = (uint32_t)&libc_realloc;
    g_storage.descriptor.entries[2].kind = PCSX_ALLOC_KIND_REALLOC;
    g_storage.descriptor.entries[2].argSize = 1;
    g_storage.descriptor.entries[2].argPtr = 0;
    g_storage.descriptor.entries[2].argCount = PCSX_ALLOCARG_NONE;

    pcsx_command(&g_storage.descriptor);
    if (!expect("register status", g_storage.descriptor.header.results.status, PCSX_CMDSTATUS_OK)) return g_check;

    if (!fetch(0)) {
        print("FAIL: report op refused\n");
        return 90;
    }
    if (!expect("arena count", g_report.arenaCount, 1)) return g_check;
    uint32_t baseAllocs = g_report.allocCount;
    uint32_t baseFrees = g_report.freeCount;
    uint32_t baseBlocks = g_report.liveBlocks;
    uint32_t baseBytes = g_report.liveBytes;

    // 3. Two allocations. This is the check that fails if the $ra return
    // capture never fires: without it nothing is ever recorded and the deltas
    // are all zero.
    void* a = psyqo_malloc(100);
    void* b = psyqo_malloc(200);
    if (!fetch(0)) return 91;
    if (!expect("allocs after 2", g_report.allocCount - baseAllocs, 2)) return g_check;
    if (!expect("blocks after 2", g_report.liveBlocks - baseBlocks, 2)) return g_check;
    if (!expect("bytes after 2", g_report.liveBytes - baseBytes, 300)) return g_check;
    if (!expect("no pending captures", g_report.pendingCaptures, 0)) return g_check;

    // 4. A plain free retires exactly one block and its bytes.
    psyqo_free(a);
    if (!fetch(0)) return 92;
    if (!expect("frees after 1", g_report.freeCount - baseFrees, 1)) return g_check;
    if (!expect("blocks after free", g_report.liveBlocks - baseBlocks, 1)) return g_check;
    if (!expect("bytes after free", g_report.liveBytes - baseBytes, 200)) return g_check;
    if (!expect("no findings yet", g_report.findingCounts[PCSX_ALLOCFINDING_DOUBLE_OR_UNKNOWN_FREE], 0)) {
        return g_check;
    }

    // 5. realloc retires the old block and records the new one, whether or not
    // the allocator moved it.
    void* c = psyqo_realloc(b, 400);
    if (!fetch(0)) return 93;
    if (!expect("blocks after realloc", g_report.liveBlocks - baseBlocks, 1)) return g_check;
    if (!expect("bytes after realloc", g_report.liveBytes - baseBytes, 400)) return g_check;

    // 6. Re-entrancy. libc_realloc gives up and does malloc + memcpy + free
    // when it cannot resize in place (common/libc/alloc.c, the tail of
    // libc_realloc), and
    // realloc(NULL, n) and realloc(p, 0) forward to malloc and free outright.
    // All three fire the registered entry points from INSIDE a tracked call.
    // Without suppression each shows up as an extra allocation or free and the
    // inner block collides with the outer one. Everything above this point
    // passes either way, which is exactly why these checks had to be added
    // rather than assumed.
    if (!fetch(0)) return 96;
    uint32_t preAllocs = g_report.allocCount;
    uint32_t preFrees = g_report.freeCount;
    uint32_t preBlocks = g_report.liveBlocks;

    // Pin a block so the one before it cannot grow in place, forcing the move.
    void* pinned = psyqo_malloc(64);
    void* movable = psyqo_malloc(64);
    void* wall = psyqo_malloc(64);
    (void)pinned;
    (void)wall;
    void* moved = psyqo_realloc(movable, 2048);
    if (!fetch(0)) return 97;
    // Three allocations plus one realloc: four recorded, never seven.
    if (!expect("reentrant realloc allocs", g_report.allocCount - preAllocs, 4)) return g_check;
    // Only the realloc's own retirement of the old block.
    if (!expect("reentrant realloc frees", g_report.freeCount - preFrees, 1)) return g_check;
    if (!expect("reentrant realloc blocks", g_report.liveBlocks - preBlocks, 3)) return g_check;
    if (!expect("no reentrant overlaps", g_report.findingCounts[PCSX_ALLOCFINDING_OVERLAP], 0)) return g_check;

    // realloc(NULL, n) is a malloc.
    preAllocs = g_report.allocCount;
    preBlocks = g_report.liveBlocks;
    void* fromNull = psyqo_realloc(nullptr, 128);
    if (!fetch(0)) return 98;
    if (!expect("realloc(NULL) allocs", g_report.allocCount - preAllocs, 1)) return g_check;
    if (!expect("realloc(NULL) blocks", g_report.liveBlocks - preBlocks, 1)) return g_check;

    // realloc(p, 0) is a free, and it returns null - so the usual
    // "null result means it failed, keep the old block" rule would strand it.
    preFrees = g_report.freeCount;
    preBlocks = g_report.liveBlocks;
    psyqo_realloc(fromNull, 0);
    if (!fetch(0)) return 99;
    if (!expect("realloc(p,0) frees", g_report.freeCount - preFrees, 1)) return g_check;
    if (!expect("realloc(p,0) blocks", preBlocks - g_report.liveBlocks, 1)) return g_check;
    psyqo_free(moved);

    // Everything past this point deliberately corrupts the heap, so nothing
    // that needs a working allocator may follow it. PSYQo does not abort on a
    // bad free the way psy-q does; it corrupts its free list, and the next
    // allocation walks that list forever. The tracker is unaffected either way
    // - it never reads guest memory - but the guest cannot continue.

    // Reserve the double-free victim BEFORE any heap corruption, and free it
    // once here. Using an older pointer instead does not work: the allocations
    // above recycle addresses, so a stale pointer can name a block that is
    // legitimately live again, and freeing it is then a correct free rather
    // than the fault we are trying to demonstrate.
    void* doomed = psyqo_malloc(48);
    psyqo_free(doomed);

    // 7. Deliberate fault: free a pointer inside a live block rather than its
    // base. This is the finding that is only possible because sizes come from
    // the calls, and it must not be confused with an unknown pointer.
    psyqo_free((void*)((uint32_t)c + 8));
    if (!fetch(0)) return 94;
    if (!expect("interior free found", g_report.findingCounts[PCSX_ALLOCFINDING_INTERIOR_FREE], 1)) return g_check;
    if (!expect("not called unknown", g_report.findingCounts[PCSX_ALLOCFINDING_DOUBLE_OR_UNKNOWN_FREE], 0)) {
        return g_check;
    }

    // 8. Deliberate fault: free something already freed. Distinct finding.
    psyqo_free(doomed);
    if (!fetch(0)) return 95;
    if (!expect("double free found", g_report.findingCounts[PCSX_ALLOCFINDING_DOUBLE_OR_UNKNOWN_FREE], 1)) {
        return g_check;
    }
    if (!expect("interior still 1", g_report.findingCounts[PCSX_ALLOCFINDING_INTERIOR_FREE], 1)) return g_check;

    // 9. A healthy allocator must produce no overlap findings across all of
    // the above. This one is about the allocator, not about the program.
    if (!expect("no overlaps", g_report.findingCounts[PCSX_ALLOCFINDING_OVERLAP], 0)) return g_check;

    // 10. Peak must have covered the largest simultaneous live set.
    if (g_report.peakBytes < 400) {
        expect("peak at least 400", g_report.peakBytes, 400);
        return g_check;
    }

    print("alloc-tracker: all ");
    printU(g_check);
    print(" checks passed\n");
    return 0;
}
