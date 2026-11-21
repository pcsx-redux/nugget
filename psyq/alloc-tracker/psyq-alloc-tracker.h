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

Ready-made allocator-tracker registrations for the Psy-Q allocators.

Describing an allocator to Redux's tracker is a dozen lines of filling in
function addresses and argument indices, and for the Sony libraries those lines
are the same for everyone forever. They belong here rather than pasted into
every program: an argument index typed wrong produces plausible garbage rather
than an error, and a copy that drifts from its original is worse than no copy.

Psy-Q ships THREE independent allocation systems (LibOver 2-23), so there are
three functions here:

  family 1   InitHeap  malloc  calloc  realloc  free      "ROM-based"
  family 2   InitHeap2 malloc2 calloc2 realloc2 free2     "RAM-based"
  family 3   InitHeap3 malloc3 calloc3 realloc3 free3     "high-speed RAM-based"

They have independent state and may be used simultaneously; registering more
than one gives you more than one arena in the viewer. Family 1 is a set of
trampolines into the kernel and carries a documented, unfixable bug - "the area
allocated cannot be completely released in free()" - so a family-1 program
leaks by design and the tracker will show exactly that. It is not a defect in
the tracker and it is worth looking at once.

Add src/mips/psyq/alloc-tracker/psyq-alloc-tracker.c to your SRCS and call one
of these once, EARLY - ideally before your InitHeap, so the tracker sees the
arena being created and takes its bounds from the call rather than needing them
supplied. Registering later works, but everything allocated beforehand is
invisible to it forever.

All of this requires the interpreter with the debugger enabled. The tracker
places execution breakpoints on the allocator's entry points, and the dynamic
recompiler honors no execution breakpoints at all.

*/

#pragma once

#include "common/hardware/pcsxcmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each returns a pcsx_CommandStatus: PCSX_CMDSTATUS_OK on success, and
   PCSX_CMDSTATUS_NO_ANSWER when this emulator has no command port at all,
   which is what an older Redux or a real console looks like. Safe to call
   unconditionally; on anything that is not a Redux with the tracker, it costs
   one store to an unclaimed address and does nothing. */
uint32_t psyq_trackMallocHeap(void);  /* family 1, the ROM allocator */
uint32_t psyq_trackMalloc2Heap(void);
uint32_t psyq_trackMalloc3Heap(void);

#ifdef __cplusplus
}
#endif
