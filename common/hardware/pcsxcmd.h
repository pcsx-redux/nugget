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

Structured command port, on the single address 0x1f8020a4.

WHY ONE ADDRESS. The EXP2 register space is not ours. Martin Korth's tooling
and the PS2 backwards-compatibility mechanism have both claimed registers in
there, and no registry says which. It is a minefield rather than a shortage, so
the right number of steps to take for all future structured features is one.

WHY THE ANSWER COMES BACK IN MEMORY, NOT IN A REGISTER. The msan helpers in
pcsxhw.h pass arguments in registers, using a hand-written load/store in an asm
block with the argument registers pinned. That is deliberate and it is the
point: the asm block forces the registers rather than hoping the allocator put
them there, so the emulator can read them out of the register file. It is a
sound technique for a small fixed-arity call.

It is the wrong shape to generalize, though. Every new operation with a new
argument list needs another hand-pinned asm block, the arity is capped by the
argument registers, an operation cannot grow a field later without a new entry
point, and nothing in it can carry a result back except another pinned
register. A structured, extensible, versioned protocol wants a struct. So here
the only channel is memory: results live in a volatile zone inside the command
struct, ordinary C ordering rules apply, and the port itself is a plain
volatile store with no register participation at all.

WHY IT IS READABLE AT ALL. A write to an unclaimed address is
INDISTINGUISHABLE FROM SUCCESS on the guest side: PCSX::HW::write32's default
case stores into the shadow register file and logs "*Unknown 32bit write". So a
program built against a newer feature, run on an older emulator, writes into a
hole and is told nothing. pcsx_present() answers "is this Redux", never "does
this Redux know command N". The fix needs no return channel: the guest poisons
results.status before issuing the command, and an emulator that does not know
the port never overwrites it. Absence of a write is the answer.

The older single-purpose registers (putc, the msan family, the PSYQo heap
metadata) are NOT migrated and stay valid forever. Breaking them would break
shipped code, and a command struct is the wrong shape for something like putc
where building the struct would cost more than the operation.

*/

#pragma once

#include <stdint.h>

#define PCSX_CMD_MAGIC 0x444d4358 /* 'XCMD' */
#define PCSX_CMD_PROTOCOL_VERSION 1
#define PCSX_CMD_PORT 0x1f8020a4

enum pcsx_Command {
    /* Reports protocol version and which operations this build implements.
       Payload is struct pcsx_QueryResult. Always implemented. */
    PCSX_CMD_QUERY = 0,
    /* Registers a generic allocator descriptor. Payload is
       struct pcsx_AllocDescriptor (allocdesc.h). May be issued repeatedly to
       track several arenas. */
    PCSX_CMD_REGISTER_ALLOCATOR = 1,
    /* Reads back what the tracker believes about one arena. Payload is
       struct pcsx_AllocReport (allocdesc.h). Exists so a guest can check the
       tracker against its own ground truth, which is the only way to find out
       whether the tracker is right rather than merely plausible. */
    PCSX_CMD_ALLOC_REPORT = 2,

    PCSX_CMD_COUNT
};

enum pcsx_CommandStatus {
    PCSX_CMDSTATUS_OK = 0,
    /* Poison value. The guest writes this before issuing a command; it
       surviving means nothing answered, which is what running on an emulator
       without this port looks like. Never written by the emulator. */
    PCSX_CMDSTATUS_NO_ANSWER = 1,
    /* The header magic did not match: a stale or uninitialized struct. */
    PCSX_CMDSTATUS_BAD_MAGIC = 2,
    /* Understood the port, does not implement that operation. */
    PCSX_CMDSTATUS_UNKNOWN_OP = 3,
    /* Understood the operation, the payload was not acceptable. */
    PCSX_CMDSTATUS_BAD_ARGUMENT = 4,
    /* The struct, or something it points at, is not in mapped guest memory. */
    PCSX_CMDSTATUS_UNMAPPED = 5,
    /* The header's declared size is too small for the operation's payload. */
    PCSX_CMDSTATUS_SHORT_STRUCT = 6,
};

/* Everything the emulator writes back lives here, and only here. Volatile
   because it changes outside the C abstract machine's view, at the volatile
   store to the port. Kept as its own zone rather than qualifying the whole
   struct so that filling in the (plain) input fields still generates ordinary
   stores. */
struct pcsx_CommandResults {
    volatile uint32_t status;
    /* Operation-specific outputs follow in each payload struct, and are
       likewise volatile. */
};

struct pcsx_CommandHeader {
    uint32_t magic;
    uint32_t op;
    /* Total size of the whole command struct in bytes, header included. This is
       how a payload grows later without burning a new op code: the emulator
       reads and writes only the fields the declared size covers. */
    uint32_t size;
    struct pcsx_CommandResults results;
};

struct pcsx_QueryResult {
    struct pcsx_CommandHeader header;
    volatile uint32_t protocolVersion;
    /* Operations 0 .. opCount-1 exist in this build's enum; the bitmap says
       which are implemented. */
    volatile uint32_t opCount;
    /* Bit N set means operation N is implemented. 128 operations. */
    volatile uint32_t opBitmap[4];
};

/* Fill in a header and poison the result. Call before pcsx_command. */
static __inline__ void pcsx_commandInit(struct pcsx_CommandHeader* header, uint32_t op, uint32_t size) {
    header->magic = PCSX_CMD_MAGIC;
    header->op = op;
    header->size = size;
    header->results.status = PCSX_CMDSTATUS_NO_ANSWER;
}

/* Issue a command. The answer is in cmd->results and in any operation-specific
   volatile fields; nothing is returned here. The memory clobber keeps the
   input-field stores from sinking past the port write, since the volatile
   store alone only orders itself against other volatile accesses. */
static __inline__ void pcsx_command(void* cmd) {
    __asm__ volatile("" ::: "memory");
    *((void* volatile* const)PCSX_CMD_PORT) = cmd;
    __asm__ volatile("" ::: "memory");
}

/* Non-zero when this emulator implements the given operation. */
static __inline__ int pcsx_commandSupported(uint32_t op) {
    struct pcsx_QueryResult query;
    if (op >= 128) return 0;
    pcsx_commandInit(&query.header, PCSX_CMD_QUERY, sizeof(query));
    query.protocolVersion = 0;
    pcsx_command(&query);
    if (query.header.results.status != PCSX_CMDSTATUS_OK) return 0;
    if (query.protocolVersion == 0) return 0;
    return (query.opBitmap[op >> 5] >> (op & 31)) & 1;
}
