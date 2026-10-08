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

#include "psyq-alloc-tracker.h"

#include <malloc.h>
#include <stdint.h>

#include "common/hardware/allocdesc.h"

/* Five entries per family: alloc, calloc, realloc, free, init. */
#define PSYQ_ENTRIES 5

/* pcsx_AllocDescriptor ends in a flexible array member and nothing may follow
   one, so the storage is a union rather than a struct with a trailing array. */
union psyq_DescriptorStorage {
    struct pcsx_AllocDescriptor descriptor;
    uint8_t raw[sizeof(struct pcsx_AllocDescriptor) + PSYQ_ENTRIES * sizeof(struct pcsx_AllocEntry)];
};

static void psyq_setEntry(struct pcsx_AllocDescriptor* descriptor, int index, uint32_t address, uint8_t kind,
                          uint8_t argSize, uint8_t argPtr, uint8_t argCount) {
    descriptor->entries[index].address = address;
    descriptor->entries[index].kind = kind;
    descriptor->entries[index].argSize = argSize;
    descriptor->entries[index].argPtr = argPtr;
    descriptor->entries[index].argCount = argCount;
}

/* The descriptor lives on the stack on purpose. The emulator reads it during
   the store to the command port, synchronously, before pcsx_command returns -
   so it does not need to outlive the call, and there is no reason to spend
   permanent BSS on something used once. */
static uint32_t psyq_registerFamily(const char* name, uint32_t allocAddr, uint32_t callocAddr, uint32_t reallocAddr,
                                    uint32_t freeAddr, uint32_t initAddr) {
    union psyq_DescriptorStorage storage;
    struct pcsx_AllocDescriptor* descriptor = &storage.descriptor;

    pcsx_allocDescriptorInit(descriptor, PSYQ_ENTRIES);
    descriptor->nameAddr = (uint32_t)name;
    /* Left unknown: the init entry below hands the tracker the real bounds
       when InitHeap runs, which beats any snapshot taken here. */
    descriptor->heapStart = 0;
    descriptor->heapEnd = 0;

    /* void *malloc(size_t)                  size in arg 0 */
    psyq_setEntry(descriptor, 0, allocAddr, PCSX_ALLOC_KIND_ALLOC, 0, PCSX_ALLOCARG_NONE, PCSX_ALLOCARG_NONE);
    /* void *calloc(size_t n, size_t sz)     count in arg 0, element size in arg 1.
       The tracked size is the product, which is why calloc has its own kind
       rather than being an alloc with a different index. */
    psyq_setEntry(descriptor, 1, callocAddr, PCSX_ALLOC_KIND_CALLOC, 1, PCSX_ALLOCARG_NONE, 0);
    /* void *realloc(void *p, size_t sz)     pointer in arg 0, size in arg 1 */
    psyq_setEntry(descriptor, 2, reallocAddr, PCSX_ALLOC_KIND_REALLOC, 1, 0, PCSX_ALLOCARG_NONE);
    /* void free(void *p)                    pointer in arg 0 */
    psyq_setEntry(descriptor, 3, freeAddr, PCSX_ALLOC_KIND_FREE, PCSX_ALLOCARG_NONE, 0, PCSX_ALLOCARG_NONE);
    /* void InitHeap(unsigned long *base, unsigned long bytes)
       base in arg 0, size in arg 1. Note the size is in BYTES for all three
       families, whatever the pointer type suggests. */
    psyq_setEntry(descriptor, 4, initAddr, PCSX_ALLOC_KIND_INIT, 1, 0, PCSX_ALLOCARG_NONE);

    pcsx_command(descriptor);
    return descriptor->header.results.status;
}

/* Family 1 entry points are trampolines - li $t2,0xA0; jr $t2; li $t1,<n> -
   rather than real functions. Breakpointing them still works and needs no
   special handling: the jump is not a call, so $ra still holds the caller and
   the kernel routine returns straight there. The argument registers are
   likewise untouched at that point. */
uint32_t psyq_trackMallocHeap(void) {
    static const char name[] = "psy-q malloc (ROM)";
    return psyq_registerFamily(name, (uint32_t)&malloc, (uint32_t)&calloc, (uint32_t)&realloc, (uint32_t)&free,
                               (uint32_t)&InitHeap);
}

uint32_t psyq_trackMalloc2Heap(void) {
    static const char name[] = "psy-q malloc2";
    return psyq_registerFamily(name, (uint32_t)&malloc2, (uint32_t)&calloc2, (uint32_t)&realloc2, (uint32_t)&free2,
                               (uint32_t)&InitHeap2);
}

uint32_t psyq_trackMalloc3Heap(void) {
    static const char name[] = "psy-q malloc3";
    return psyq_registerFamily(name, (uint32_t)&malloc3, (uint32_t)&calloc3, (uint32_t)&realloc3, (uint32_t)&free3,
                               (uint32_t)&InitHeap3);
}
