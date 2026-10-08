#include <malloc.h>
#include <stdint.h>

#include "alloc-tracker/psyq-alloc-tracker.h"
#include "common/hardware/allocdesc.h"
#include "common/hardware/pcsxcmd.h"
#include "common/hardware/pcsxhw.h"

/* A heap this program owns outright. InitHeap3 wants a size that is a multiple
   of 8 and silently discards any remainder. */
#define HEAP_WORDS 4096
static unsigned long s_heap[HEAP_WORDS];

/* pcsx_putc output is assembled on the host and only flushed when it sees a
   newline, so a line without one is never printed at all. */
static void print(const char* s) {
    while (*s) pcsx_putc(*s++);
}

static void printU(uint32_t value) {
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

static void line(const char* label, uint32_t value) {
    print(label);
    printU(value);
    print("\n");
}

static struct pcsx_AllocReport s_report;

/* Returning from main under the psyq crt0 lands in A0:A1, which is abort().
   psyqo's cxxglue calls pcsx_exit(main()) for you; this crt0 does not, so say
   it explicitly. */
static void finish(int code) {
    pcsx_exit(code);
}

static int fetch(void) {
    pcsx_allocReportInit(&s_report, 0);
    pcsx_command(&s_report);
    return s_report.header.results.status == PCSX_CMDSTATUS_OK;
}

int main(void) {
    print("psyq malloc3 tracker example\n");

    /* Ask before assuming. On an emulator without the command port the poison
       in results.status survives untouched and this comes back false, rather
       than the write silently landing in a shadow register. */
    if (!pcsx_commandSupported(PCSX_CMD_REGISTER_ALLOCATOR)) {
        print("this emulator has no allocator tracker; nothing to demonstrate\n");
        finish(0);
        return 0;
    }

    /* One call. The descriptor - five entry points, their kinds, and which
       argument carries what - lives in the library, because for the Sony
       allocators those details are identical for every program that ever uses
       them and an argument index typed wrong yields plausible garbage rather
       than an error. psyq_trackMallocHeap and psyq_trackMalloc2Heap register
       the other two families the same way; registering several gives several
       arenas in the viewer. */
    uint32_t status = psyq_trackMalloc3Heap();
    if (status != PCSX_CMDSTATUS_OK) {
        line("registration failed, status ", status);
        finish(1);
        return 1;
    }

    /* Registered BEFORE InitHeap3 runs, so the tracker sees the arena being
       created and picks up its bounds from the call. */
    InitHeap3(s_heap, sizeof(s_heap));

    void* a = malloc3(100);
    void* b = calloc3(16, 20); /* 320 bytes */
    void* c = malloc3(64);
    free3(c);
    b = realloc3(b, 640);

    if (!fetch()) {
        print("report refused\n");
        finish(2);
        return 2;
    }

    print("--- what the tracker saw ---\n");
    line("arenas:        ", s_report.arenaCount);
    line("live blocks:   ", s_report.liveBlocks);
    line("live bytes:    ", s_report.liveBytes);
    line("peak bytes:    ", s_report.peakBytes);
    line("allocations:   ", s_report.allocCount);
    line("frees:         ", s_report.freeCount);

    /* Ground truth: a (100) and b (640 after the realloc) are live; c was
       freed. calloc3's 320 bytes must have been recorded as the product of its
       two arguments, and the realloc must have retired the old block rather
       than counting both. */
    /* Four calls in, four recorded. calloc3 and realloc3 are built on malloc3
       and free3, so without re-entrancy suppression these read 6 and 3 and the
       inner blocks collide with the outer ones. */
    if (s_report.allocCount != 4 || s_report.freeCount != 2) {
        print("MISMATCH: allocator internals were counted as guest calls\n");
        finish(7);
        return 7;
    }
    if (s_report.liveBlocks != 2 || s_report.liveBytes != 740) {
        print("MISMATCH: expected 2 blocks / 740 bytes\n");
        finish(3);
        return 3;
    }
    /* Peak covers a + b(320) + c(64) before anything was freed. */
    if (s_report.peakBytes < 484) {
        print("MISMATCH: peak too low\n");
        finish(4);
        return 4;
    }
    if (s_report.findingCounts[PCSX_ALLOCFINDING_OVERLAP] != 0) {
        print("MISMATCH: allocator handed out overlapping blocks\n");
        finish(5);
        return 5;
    }

    /* No deliberate heap fault here, and the reason is worth knowing before you
       try one. Handing free3() a pointer it never issued corrupts psy-q's free
       list, and the program aborts through A0:A1 before it can read the finding
       back. The tracker DOES record it - it never touches guest memory, so a
       wrecked heap does not impair it - but the guest does not survive to say
       so. PSYQo's allocator happens to tolerate the same abuse, so the
       findings-panel demonstration lives in src/mips/tests/alloc-tracker
       instead. Against a real allocator, read the findings in the widget rather
       than expecting the program to keep running. */

    print("psyq malloc3 tracker example: ok\n");
    finish(0);
        return 0;
}
