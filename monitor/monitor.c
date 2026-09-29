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

#include "monitor/monitor.h"

#include <stddef.h>

#include "common/psxlibc/handlers.h"
#include "common/syscalls/syscalls.h"
#include "monitor/cop0dbg.h"
#include "monitor/install.h"
#include "monitor/kernel.h"
#include "common/kernel/threads.h"
#include "monitor/link.h"
#include "monitor/transport.h"

#ifdef MONITOR_LZ4
#define LZ4STREAM_TRUSTED 1
#include "monitor/lz4stream.c"
#define MON_CAPS_LZ4 MON_CAP_LZ4
#else
#define MON_CAPS_LZ4 0
#endif

/* A link that says where its received-byte flag is can take STOP while the
   target runs (see monitorSlotEntry). ATCONS does not. */
#ifdef LINK_RX_STAT_ADDR
#define MON_CAPS_STOP MON_CAP_STOP
#else
#define MON_CAPS_STOP 0
#endif

#define MON_CAPS (MON_CAPS_LZ4 | MON_CAPS_STOP)

/* READ_MEM responses stream out of target memory in 8 KiB chunks (design
   section 13). Nothing is staged: bulk data goes straight to/from the operation's
   real address, so the only buffer the monitor needs is a tiny one for the small
   fixed-size commands (RUN carries the most at 6 words). */
#define MON_CHUNK_BYTES 8192
#define MON_CMD_WORDS 16

/* All of the monitor's state in one object: the compiler then reaches every
   field from one base register instead of materializing an address per
   variable (there is no gp-relative data on this target). */
static struct {
    uint16_t cmd[MON_CMD_WORDS];
    /* Saved context of the halted program (== the current thread's register
       frame, which the kernel exception entry fills). NULL before the first
       stop and while running. */
    struct Registers *ctx;
    /* BadVaddr captured at the last fault (struct Registers has no slot for it). */
    uint32_t badVaddr;
    /* Running DCIC image + the armed data-watch address, so SET_BP/CLR_BP can
       add/remove one breakpoint kind and the stop path can report the watch. */
    uint32_t dcic;
    uint32_t watchAddr;
    /* Set when WRITE_MEM or LOAD wrote memory since the last resume. */
    int memWritten;
    /* Set from RUN/CONT until the next stop: only then is a byte on the link
       a STOP request. Before the first RUN it is the host's next command. */
    int running;
#ifdef MONITOR_LZ4
    /* The LZ4 stream in flight: its decoder, how much of clen has arrived, and
       whether one is open at all (see streamWriteMemLz4). */
    struct Lz4Stream lz;
    uint32_t lzConsumed;
    int lzActive;
#endif
} s_mon;

/* SR the target runs under after a fresh RUN. The rfe in returnFromException
   pops IEp->IEc, so IEp (bit2) enables interrupts; IM2 (bit10) unmasks the PS1
   IRQ line; CU2 (bit30) keeps the GTE usable. BRING-UP KNOB: unverified on
   silicon - adjust if a launched binary needs a different privilege/IRQ state. */
#define MON_RUN_SR 0x40000404u

/* ---- small helpers ---- */

static struct Registers *currentRegs(void) { return &__globals.processes[0].thread->registers; }

/* Resume the target. Anything written may be code the I-cache still holds
   older lines for, so flush first when memory changed. */
static inline __attribute__((noreturn)) void monitorResume(void) {
    s_mon.running = 1;
    if (s_mon.memWritten) {
        s_mon.memWritten = 0;
        syscall_flushCache();
    }
    syscall_returnFromException();
}

static uint32_t rd32(const uint16_t *p, unsigned i) { return (uint32_t)p[i] | ((uint32_t)p[i + 1] << 16); }

static void sendWord32(uint32_t v) {
    transportSendWord((uint16_t)(v & 0xffff));
    transportSendWord((uint16_t)(v >> 16));
}

/* A command's reply: 0 is an ACK, a MON_E* code is an ERROR carrying it, and
   MON_REPLIED means the handler already answered (DATA, REGS, PONG). */
#define MON_REPLIED (-1)

static void sendStatus(int code) {
    uint16_t c = (uint16_t)code;
    transportSendBegin(code ? MON_ERROR : MON_ACK, code != 0);
    if (code) transportSendWord(c);
    transportSendEnd();
}

/* ---- events ---- */

/* Fletcher-32 of the 512 KiB BIOS region, set once before the monitor is
   entered (see monitorBiosChecksum). */
uint32_t s_biosChecksum;

/* The kernel exception handler patch slot monitorHook() pointed at
   monitorSlotEntry, or 0 when it left the handler alone (install.h). */
uint32_t *s_monitorSlot;

/* HELLO and PONG: [proto_ver:u16][caps:u16][bios_fletcher32:u32]. PONG
   carries it too, so a host that attaches after boot can still ask. */
static void emitIdentity(uint16_t type) {
    transportSendBegin(type, 4);
    transportSendWord(MON_PROTO_VER);
    transportSendWord(MON_CAPS | (s_monitorSlot ? MON_CAP_SLOT : 0));
    sendWord32(s_biosChecksum);
    transportSendEnd();
}

/* STOPPED [reason:u16][epc:u32][a:u32][b:u32] */
static void emitStopped(uint16_t reason, uint32_t epc, uint32_t a, uint32_t b) {
    transportSendBegin(MON_STOPPED, 7);
    transportSendWord(reason);
    sendWord32(epc);
    sendWord32(a);
    sendWord32(b);
    transportSendEnd();
}

/* ---- inspection / control command handlers ---- */

/* READ_MEM [addr:u32][len:u32] -> one or more DATA [nbytes:u32][bytes] frames,
   each capped at an 8 KiB chunk; the host concatenates until it has len bytes. */
static int cmdReadMem(const uint16_t *p) {
    uint32_t addr = rd32(p, 0);
    uint32_t len = rd32(p, 2);
    const uint8_t *src = (const uint8_t *)addr;

    uint32_t off = 0;
    do {
        uint32_t chunk = len - off;
        if (chunk > MON_CHUNK_BYTES) chunk = MON_CHUNK_BYTES;
        uint32_t words = (chunk + 1) >> 1;

        transportSendBegin(MON_DATA, (uint16_t)(2 + words));
        sendWord32(chunk); /* nbytes in THIS frame */
        for (uint32_t i = 0; i < words; i++) {
            uint32_t bi = off + 2 * i;
            uint8_t b0 = src[bi];
            uint8_t b1 = (2 * i + 1 < chunk) ? src[bi + 1] : 0;
            transportSendWord((uint16_t)(b0 | (b1 << 8)));
        }
        transportSendEnd();
        off += chunk;
    } while (off < len);
    return MON_REPLIED;
}

static uint32_t recvU32(void) {
    uint32_t lo = transportRecvWord();
    uint32_t hi = transportRecvWord();
    return lo | (hi << 16);
}

/* WRITE_MEM / LOAD [addr:u32][len:u32][bytes] -> ACK | ERROR. Called mid-frame:
   transportRecvBegin has already consumed TYPE and LEN. The bytes are decoded
   straight into the target address as they arrive - no staging buffer - so a
   single frame carries an arbitrarily large write (host still chunks at 8 KiB).
   `frameWords` is the payload word count from the frame header. */
static int streamWriteMem(uint16_t frameWords) {
    if (frameWords < 4) {
        for (uint16_t w = 0; w < frameWords; w++) transportRecvWord();
        transportRecvEnd();
        return MON_EBADLEN;
    }
    uint32_t addr = recvU32();   /* 2 words */
    uint32_t nbytes = recvU32(); /* 2 words */
    uint32_t consumed = 4;
    uint8_t *dst = (uint8_t *)addr;
    s_mon.memWritten = 1;

    for (uint16_t w = consumed; w < frameWords; w++) {
        uint16_t word = transportRecvWord();
        uint32_t bi = (uint32_t)(w - consumed) * 2;
        if (bi < nbytes) dst[bi] = (uint8_t)(word & 0xff);
        if (bi + 1 < nbytes) dst[bi + 1] = (uint8_t)(word >> 8);
    }

    return transportRecvEnd() == TRANSPORT_OK ? 0 : MON_ECKSUM;
}

#ifdef MONITOR_LZ4
/* WRITE_MEM / LOAD with MON_LZ4: [dest:u32][rawlen:u32][clen:u32][off:u32]
   [nbytes:u32][bytes]. The frames carry consecutive slices of one LZ4 block
   stream of clen bytes that decodes to rawlen bytes at dest; off is where this
   slice starts in it, and 0 starts a new stream. Bytes are decoded as they
   arrive, so every frame is ACKed on its own; the last one also checks the
   stream ended cleanly at exactly rawlen bytes. Any failure drops the stream,
   and the host starts over from off 0. */
static int streamWriteMemLz4(uint16_t frameWords) {
    uint32_t words = frameWords;
    uint32_t rawLen = 0, clen = 0, nbytes = 0;
    int bad = 0;

    s_mon.memWritten = 1;
    if (words < 10) {
        bad = MON_EBADLEN; /* the rest of the frame is drained below */
    } else {
        uint32_t dest = recvU32();
        rawLen = recvU32();
        clen = recvU32();
        uint32_t off = recvU32();
        nbytes = recvU32();
        words -= 10;
        if (off == 0) {
            lz4StreamInit(&s_mon.lz, (void *)dest);
            s_mon.lzConsumed = 0;
            s_mon.lzActive = 1;
        } else if (!s_mon.lzActive || off != s_mon.lzConsumed) {
            bad = MON_EBADSTATE;
        }
        if (!bad && (nbytes > words * 2 || off + nbytes > clen)) bad = MON_EBADLEN;
    }

    for (uint32_t w = 0; w < words; w++) {
        uint16_t word = transportRecvWord();
        if (bad) continue;
        /* Low byte then high byte, as far as nbytes reaches. */
        for (uint32_t bi = w * 2; bi < w * 2 + 2 && bi < nbytes; bi++, word >>= 8) {
            if (!bad && lz4StreamFeed(&s_mon.lz, (uint8_t)word)) bad = MON_EDECODE;
        }
    }
    if (transportRecvEnd() != TRANSPORT_OK) bad = MON_ECKSUM;
    if (!bad) {
        s_mon.lzConsumed += nbytes;
        if (s_mon.lzConsumed == clen) {
            if (lz4StreamEndBlock(&s_mon.lz) || (uint32_t)(s_mon.lz.out - s_mon.lz.base) != rawLen) bad = MON_EDECODE;
            s_mon.lzActive = 0;
        }
    }
    if (bad) s_mon.lzActive = 0;
    return bad;
}
#endif

/* The REGS table, gdb g-packet order: 0..31 r0..r31, then SR, LO, HI,
   BadVaddr, Cause, PC. Where entry idx lives; BadVaddr has no slot in struct
   Registers and lives in s_mon. */
static uint32_t *regSlot(struct Registers *r, unsigned idx) {
    static const uint8_t special[] = {
        offsetof(struct Registers, SR),    offsetof(struct Registers, lo),    offsetof(struct Registers, hi), 0,
        offsetof(struct Registers, Cause), offsetof(struct Registers, returnPC),
    };
    if (idx < 32) return &r->GPR.r[idx];
    if (idx == 35) return &s_mon.badVaddr;
    return (uint32_t *)((uint8_t *)r + special[idx - 32]);
}

/* GET_REGS [] -> REGS [38 x u32]. Zeros for the registers before the first
   stop; BadVaddr is reported regardless. */
static int cmdGetRegs(void) {
    struct Registers *r = s_mon.ctx;
    transportSendBegin(MON_REGS, 38 * 2);
    for (unsigned i = 0; i < 38; i++) sendWord32((r || i == 35) ? *regSlot(r, i) : 0);
    transportSendEnd();
    return MON_REPLIED;
}

/* SET_REG [idx:u16][value:u32] -> ACK | ERROR. idx indexes the REGS table. */
static int cmdSetReg(const uint16_t *p) {
    uint16_t idx = p[0];
    uint32_t val = rd32(p, 1);
    struct Registers *r = s_mon.ctx;

    if (idx > 37) return MON_EBADREG;
    if (!r) return MON_EBADSTATE;
    if (idx != 0) *regSlot(r, idx) = val; /* r0 stays 0 */
    return 0;
}

/* SET_BP [kind:u16][addr:u32][mask:u32] -> ACK.
   kind: 0 exec, 1 data-read, 2 data-write, 3 data-rw. */
static int cmdSetBp(const uint16_t *p) {
    uint16_t kind = p[0];
    uint32_t addr = rd32(p, 1);
    uint32_t mask = rd32(p, 3);

    if (kind == 0) {
        writeBPC(addr);
        writeBPCM(mask);
        s_mon.dcic |= DCIC_DE | DCIC_PCE | DCIC_TR | DCIC_KD | DCIC_UD;
    } else if (kind <= 3) {
        writeBDA(addr);
        writeBDAM(mask);
        s_mon.watchAddr = addr;
        /* kind bit0 is read, bit1 is write; DR and DW sit in that order in DCIC. */
        s_mon.dcic |= DCIC_DE | DCIC_DAE | DCIC_TR | DCIC_KD | DCIC_UD | (uint32_t)kind * DCIC_DR;
    } else {
        return MON_EBADCMD;
    }
    writeDCIC(s_mon.dcic);
    return 0;
}

/* CLR_BP [kind:u16] -> ACK. */
static int cmdClrBp(const uint16_t *p) {
    uint16_t kind = p[0];
    if (kind == 0) {
        s_mon.dcic &= ~DCIC_PCE;
    } else {
        s_mon.dcic &= ~(DCIC_DAE | DCIC_DR | DCIC_DW);
    }
    if ((s_mon.dcic & (DCIC_PCE | DCIC_DAE)) == 0) s_mon.dcic = 0; /* drop master enable */
    writeDCIC(s_mon.dcic);
    return 0;
}

/* RUN [pc:u32][gp:u32][sp:u32] -> ACK, then hand the CPU to the target. Never
   returns (returnFromException does an rfe into pc). */
static __attribute__((noreturn)) void cmdRun(const uint16_t *p) {
    uint32_t pc = rd32(p, 0);
    uint32_t gp = rd32(p, 2);
    uint32_t sp = rd32(p, 4);
    struct Registers *r = currentRegs();

    /* Everything zero except what the target starts from. */
    for (unsigned i = 0; i < sizeof(*r) / sizeof(uint32_t); i++) ((uint32_t *)r)[i] = 0;
    r->GPR.n.gp = gp;
    r->GPR.n.sp = sp;
    r->GPR.n.fp = sp;
    r->returnPC = pc;
    r->SR = MON_RUN_SR;
    s_mon.ctx = 0; /* running: no halted context */

    sendStatus(0);
    monitorResume();
}

/* CONT [] -> ACK, then resume the saved (possibly SET_REG-modified) context. */
static int cmdCont(void) {
    if (!s_mon.ctx) return MON_EBADSTATE;
    s_mon.ctx = 0; /* running: no halted context */
    sendStatus(0);
    monitorResume();
}

/* SET_BAUD [reload:u16] -> ACK at the old rate, then PONG at the new one if
   the host's PING arrives there (design section 2a). */
static int cmdSetBaud(const uint16_t *p) {
    uint16_t reload = p[0];
    if (reload == 0) return MON_EBADLEN;
    if (!transportHasRate()) return MON_EBADCMD;
    sendStatus(0);
    uint16_t ver = MON_PROTO_VER;
    if (transportTryRate(reload, MON_PONG, ver) == 1) transportSendFrame(MON_PONG, &ver, 1);
    return MON_REPLIED;
}

/* Dispatch one small HALTED-state command whose payload is already buffered in
   s_mon.cmd. WRITE_MEM/LOAD are NOT here - they stream directly to target memory in
   the loop. RUN does not return (it resumes the target). Returns the reply. */
static int dispatchCommand(uint16_t type, const uint16_t *payload) {
    switch (type) {
        case MON_PING:
            emitIdentity(MON_PONG);
            return MON_REPLIED;
        case MON_READ_MEM: return cmdReadMem(payload);
        case MON_GET_REGS: return cmdGetRegs();
        case MON_SET_REG: return cmdSetReg(payload);
        case MON_SET_BP: return cmdSetBp(payload);
        case MON_CLR_BP: return cmdClrBp(payload);
        case MON_RUN: cmdRun(payload); /* does not return */
        case MON_CONT: return cmdCont();
        case MON_SET_BAUD: return cmdSetBaud(payload);
        case MON_STOP:
            /* Only meaningful while RUNNING, where the exception entry reads
               it (monitorTake). In HALTED it is a no-op with no reply: a host
               whose STOP crossed a stop on the wire must not get an ACK it
               cannot match to a command. */
            return MON_REPLIED;
        default: return MON_EBADCMD;
    }
}

/* Blocking HALTED command loop. Bulk WRITE_MEM/LOAD payloads are decoded
   straight into target memory (no staging); every other command has a small
   fixed payload buffered in s_mon.cmd. Returns only via a resume, which is noreturn,
   so in practice it does not return. */
static __attribute__((noreturn)) void monitorCommandLoop(void) {
    for (;;) {
        uint16_t type;
        uint16_t len;
        transportRecvBegin(&type, &len);

        int reply;
        uint16_t base = type & ~MON_LZ4;
        if (base == MON_WRITE_MEM || base == MON_LOAD) {
            if (!(type & MON_LZ4)) {
                reply = streamWriteMem(len);
            } else {
#ifdef MONITOR_LZ4
                reply = streamWriteMemLz4(len);
#else
                for (uint16_t i = 0; i < len; i++) transportRecvWord();
                transportRecvEnd();
                reply = MON_EBADCMD;
#endif
            }
        } else {
            for (uint16_t i = 0; i < len; i++) {
                uint16_t w = transportRecvWord();
                if (i < MON_CMD_WORDS) s_mon.cmd[i] = w; /* the rest is drained */
            }
            if (transportRecvEnd() != TRANSPORT_OK) {
                reply = MON_ECKSUM;
            } else if (len > MON_CMD_WORDS) {
                reply = MON_EBADLEN;
            } else {
                reply = dispatchCommand(type, s_mon.cmd);
            }
        }
        if (reply != MON_REPLIED) sendStatus(reply);
    }
}

/* Snapshot a stop, tell the host, and drop into the command loop until the host
   resumes. Never returns. */
static __attribute__((noreturn)) void monitorStop(struct Registers *r, uint16_t reason, uint32_t a, uint32_t b) {
    s_mon.running = 0;
    s_mon.ctx = r;
    s_mon.badVaddr = (reason == MON_STOP_FAULT) ? b : 0;
    emitStopped(reason, r->returnPC, a, b);
    monitorCommandLoop();
    __builtin_unreachable();
}

/* ---- exception entry ---- */

/* What the monitor owns, for either entry below. `r` is the current thread's
   register frame with the whole context in it. Returns for exceptions that
   are not the monitor's; when it owns one it never returns (it resumes the
   target or enters the command loop). */
static __attribute__((noinline)) void monitorTake(struct Registers *r) {
    uint32_t excode = CAUSE_EXCCODE(r->Cause);

    if (excode == EXCCODE_BP) {
        uint32_t insn = *(uint32_t *)(r->returnPC);
        if ((insn & 0x3f) == 0x0d) {
            /* Software `break`. `break 4, 1` is the monitor's own entry; every
               other one stops, with the instruction word in a and EPC left on
               the break. What it means is the host's business, and so is
               stepping past it. */
            if (insn == ((4u << 16) | (1u << 6) | 0x0d)) {
                s_mon.running = 0;
                emitIdentity(MON_HELLO);
                monitorCommandLoop();
            }
            monitorStop(r, MON_STOP_BREAKPOINT, insn, 0);
        } else {
            /* No `break` at EPC: a cop0 hardware breakpoint. Disable the debug
               unit first (design section 10), then report. */
            uint32_t dcic = readDCIC();
            writeDCIC(0);
            s_mon.dcic = 0;
            if (dcic & DCIC_DA) {
                monitorStop(r, MON_STOP_DATA_WATCH, s_mon.watchAddr, 0);
            } else {
                monitorStop(r, MON_STOP_BREAKPOINT, 0, 0);
            }
        }
    }

    if (excode == EXCCODE_ADEL || excode == EXCCODE_ADES) {
        monitorStop(r, MON_STOP_FAULT, excode, readBadVaddr());
    }
    if (excode == EXCCODE_IBE || excode == EXCCODE_DBE || excode == EXCCODE_RI || excode == EXCCODE_CPU ||
        excode == EXCCODE_OVF) {
        monitorStop(r, MON_STOP_FAULT, excode, 0);
    }

    /* An interrupt while a target runs, with the host's STOP on the link:
       halt here. The IRQ that brought us in stays pending and unacknowledged
       in I_STAT, so after CONT the kernel and the program take it as if the
       monitor had not been there. */
    if (excode == EXCCODE_INT && s_mon.running && transportStopPending()) {
        monitorStop(r, MON_STOP_INTERRUPT, 0, 0);
    }

    /* Syscall (8), any other interrupt, anything else: not ours. */
}

/* Entry 1, the kernel's chain: a priority-0 verifier, prepended ahead of the
   kernel's own, called after the kernel has saved the whole context and
   switched to its exception stack. It is lost if a program resets the
   priority-0 chain, which is what entry 2 is for. Returns 0 for what it
   does not own, so the rest of the chain runs. */
static int monitorVerifier(void) {
    monitorTake(currentRegs());
    return 0;
}

struct HandlerInfo s_monitorHandler = {
    .next = 0,
    .handler = 0,
    .verifier = monitorVerifier,
    .padding = 0,
};

/* Entry 2, the kernel exception handler's fourth patch slot (install.h puts
   `lui at / ori at / jalr at / nop` there, calling monitorSlotEntry). The
   slot runs before any chain, with only at, v0, v1 and ra saved in the frame
   k0 points at (and the resume PC at +0x80); those four plus ra are all it
   may touch before deciding. monitorSlotEntry keeps its own exceptions
   (break, the faults above, an interrupt with a byte on the link) and
   returns straight to the kernel for everything else. For its own, it saves
   the rest of the context into the frame exactly as the kernel's code after
   the slots would, moves to its own stack and calls monitorSlotDispatch. If
   that returns (an interrupt that was console text, or came before the first
   RUN), it puts back every register the C code may have changed and returns
   to the kernel, which saves the same values again and runs its chains; the
   verifier above then declines the exception too. Nothing it keeps ever
   reaches the chains, so no exception is handled twice. */
#ifndef MONITOR_SLOT_STACK_WORDS
#define MONITOR_SLOT_STACK_WORDS 256
#endif
uint32_t monitorSlotStack[MONITOR_SLOT_STACK_WORDS] __attribute__((used, aligned(8)));

void __attribute__((used, noinline)) monitorSlotDispatch(void) { monitorTake(currentRegs()); }

/* ExcCodes 4-7, 9-12 as a bit mask: AdEL, AdES, IBE, DBE, Bp, RI, CpU, Ov. */
#define MON_SLOT_EXCMASK 0x1ef0

#define MON_STR_(x) #x
#define MON_STR(x) MON_STR_(x)

#ifdef LINK_RX_STAT_ADDR
/* A link whose status bit reads 0 for "byte waiting" sets this to "beqz". */
#ifndef LINK_RX_STAT_BRANCH
#define LINK_RX_STAT_BRANCH "bnez"
#endif
#define MON_SLOT_RX_TEST                                                                    \
    "    li    $v1, " MON_STR(LINK_RX_STAT_ADDR) "\n"                                        \
    "    " LINK_RX_STAT_LOAD " $v1, 0($v1)\n"                                                \
    "    nop\n"                                                                              \
    "    andi  $v1, $v1, " MON_STR(LINK_RX_STAT_BIT) "\n"                                   \
    "    " LINK_RX_STAT_BRANCH " $v1, 2f\n"                                                  \
    "    nop\n"
#else
#define MON_SLOT_RX_TEST
#endif

__asm__(
    "    .section .text.monitorSlotEntry, \"ax\", @progbits\n"
    "    .align 2\n"
    "    .global monitorSlotEntry\n"
    "    .type monitorSlotEntry, @function\n"
    "    .set push\n"
    "    .set noreorder\n"
    "    .set noat\n"
    "monitorSlotEntry:\n"
    /* v0 = ExcCode */
    "    mfc0  $v0, $13\n"
    "    nop\n"
    "    andi  $v0, $v0, 0x7c\n"
    "    beqz  $v0, 1f\n"
    "    srl   $v0, $v0, 2\n"
    "    li    $v1, " MON_STR(MON_SLOT_EXCMASK) "\n"
    "    srlv  $v1, $v1, $v0\n"
    "    andi  $v1, $v1, 1\n"
    "    bnez  $v1, 2f\n"
    "    nop\n"
    "    jr    $ra\n"
    "    nop\n"
    /* Interrupt: ours only if the link has a byte waiting. */
    "1:\n" MON_SLOT_RX_TEST
    "    jr    $ra\n"
    "    nop\n"
    /* Ours: the kernel's own save sequence, same order, same offsets. */
    "2:\n"
    "    sw    $a0, 0x10($k0)\n"
    "    sw    $a1, 0x14($k0)\n"
    "    sw    $a2, 0x18($k0)\n"
    "    sw    $a3, 0x1c($k0)\n"
    "    mfc0  $a0, $12\n"
    "    nop\n"
    "    sw    $a0, 0x8c($k0)\n"
    "    mfc0  $a1, $13\n"
    "    nop\n"
    "    sw    $a1, 0x90($k0)\n"
    "    sw    $k1, 0x6c($k0)\n"
    "    sw    $s0, 0x40($k0)\n"
    "    sw    $s1, 0x44($k0)\n"
    "    sw    $s2, 0x48($k0)\n"
    "    sw    $s3, 0x4c($k0)\n"
    "    sw    $s4, 0x50($k0)\n"
    "    sw    $s5, 0x54($k0)\n"
    "    sw    $s6, 0x58($k0)\n"
    "    sw    $s7, 0x5c($k0)\n"
    "    sw    $t0, 0x20($k0)\n"
    "    sw    $t1, 0x24($k0)\n"
    "    sw    $t2, 0x28($k0)\n"
    "    sw    $t3, 0x2c($k0)\n"
    "    sw    $t4, 0x30($k0)\n"
    "    sw    $t5, 0x34($k0)\n"
    "    sw    $t6, 0x38($k0)\n"
    "    sw    $t7, 0x3c($k0)\n"
    "    sw    $t8, 0x60($k0)\n"
    "    sw    $t9, 0x64($k0)\n"
    "    sw    $gp, 0x70($k0)\n"
    "    sw    $sp, 0x74($k0)\n"
    "    sw    $fp, 0x78($k0)\n"
    "    mfhi  $a0\n"
    "    nop\n"
    "    sw    $a0, 0x84($k0)\n"
    "    mflo  $a0\n"
    "    nop\n"
    "    sw    $a0, 0x88($k0)\n"
    /* s0 (saved above, callee-saved in C) keeps the way back into the
       kernel's handler. */
    "    move  $s0, $ra\n"
    "    la    $sp, monitorSlotStack + " MON_STR(MONITOR_SLOT_STACK_WORDS) " * 4 - 16\n"
    "    jal   monitorSlotDispatch\n"
    "    nop\n"
    /* Declined: the frame pointer again (k0 is the kernel's), the registers
       C may have changed, and back to the kernel. */
    "    lw    $k0, 0x108($zero)\n" /* the table of tables at 0x100: ->processes */
    "    move  $ra, $s0\n"
    "    lw    $k0, 0($k0)\n"
    "    nop\n"
    "    addiu $k0, $k0, 8\n"
    "    lw    $a0, 0x84($k0)\n"
    "    lw    $a1, 0x88($k0)\n"
    "    mthi  $a0\n"
    "    mtlo  $a1\n"
    "    lw    $a0, 0x10($k0)\n"
    "    lw    $a1, 0x14($k0)\n"
    "    lw    $a2, 0x18($k0)\n"
    "    lw    $a3, 0x1c($k0)\n"
    "    lw    $t0, 0x20($k0)\n"
    "    lw    $t1, 0x24($k0)\n"
    "    lw    $t2, 0x28($k0)\n"
    "    lw    $t3, 0x2c($k0)\n"
    "    lw    $t4, 0x30($k0)\n"
    "    lw    $t5, 0x34($k0)\n"
    "    lw    $t6, 0x38($k0)\n"
    "    lw    $t7, 0x3c($k0)\n"
    "    lw    $t8, 0x60($k0)\n"
    "    lw    $t9, 0x64($k0)\n"
    "    lw    $s0, 0x40($k0)\n"
    "    lw    $sp, 0x74($k0)\n"
    "    jr    $ra\n"
    "    nop\n"
    "    .set pop\n"
    "    .size monitorSlotEntry, . - monitorSlotEntry\n"
    "    .previous\n");

void monitorMain(void) {
    psxprintf("OpenBIOS Monitor.\n");
    transportInit();
    s_mon.ctx = 0;
    s_mon.badVaddr = 0;
    s_mon.dcic = 0;
    s_mon.watchAddr = 0;
    s_mon.running = 0;

    s_biosChecksum = monitorBiosChecksum();
    monitorHook();
    monitorEnter();
}
