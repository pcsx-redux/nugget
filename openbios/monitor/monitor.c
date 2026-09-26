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

#include "openbios/monitor/stagemark.h"
#include "openbios/monitor/monitor.h"

#include "common/psxlibc/handlers.h"
#include "common/syscalls/syscalls.h"
#include "openbios/fileio/fileio.h"
#include "openbios/kernel/globals.h"
#include "openbios/kernel/handlers.h"
#include "openbios/monitor/cop0dbg.h"
#include "openbios/monitor/pcdrv.h"
#include "openbios/monitor/transport.h"

/* READ_MEM responses stream out of target memory in 8 KiB chunks (design
   section 13). Nothing is staged: bulk data goes straight to/from the operation's
   real address, so the only buffer the monitor needs is a tiny one for the small
   fixed-size commands (RUN carries the most at 6 words). */
#define MON_CHUNK_BYTES 8192
#define MON_CMD_WORDS 16

static uint16_t s_cmd[MON_CMD_WORDS];

/* Saved context of the halted program (== the current thread's register frame,
   which the kernel exception entry fills). NULL before the first stop. */
static struct Registers *s_ctx;

/* BadVaddr captured at the last fault (struct Registers has no slot for it). */
static uint32_t s_badVaddr;

/* Running DCIC image + the armed data-watch address, so SET_BP/CLR_BP can
   add/remove one breakpoint kind and the stop path can report the watch. */
static uint32_t s_dcic;
static uint32_t s_watchAddr;

/* SR the target runs under after a fresh RUN. The rfe in returnFromException
   pops IEp->IEc, so IEp (bit2) enables interrupts; IM2 (bit10) unmasks the PS1
   IRQ line; CU2 (bit30) keeps the GTE usable. BRING-UP KNOB: unverified on
   silicon - adjust if a launched binary needs a different privilege/IRQ state. */
#define MON_RUN_SR 0x40000404u

/* ---- small helpers ---- */

static struct Registers *currentRegs(void) { return &__globals.processes[0].thread->registers; }

static inline __attribute__((noreturn)) void monitorResume(void) { syscall_returnFromException(); }

static uint32_t rd32(const uint16_t *p, unsigned i) { return (uint32_t)p[i] | ((uint32_t)p[i + 1] << 16); }

static void sendWord32(uint32_t v) {
    transportSendWord((uint16_t)(v & 0xffff));
    transportSendWord((uint16_t)(v >> 16));
}

static void sendAck(void) { transportSendFrame(MON_ACK, (uint16_t[]){0}, 0); }

static void sendError(uint16_t code) { transportSendFrame(MON_ERROR, &code, 1); }

static inline __attribute__((noreturn)) void monitorEnter() {
    __asm__ volatile("break 4, 1\n" : : : "memory");
}

/* ---- events ---- */

/* HELLO [proto_ver:u16][sram_base:u32][ram_size:u32] */
static void emitHello(void) {
    /* Announce the RAM size the kernel actually detected rather than a
       hardcoded constant. __globals60.ramsize is in megabytes (the reset path
       sets it; the H2x00 devkit carries 8 MiB vs retail's 2 MiB). */
    uint32_t ramSize = __globals60.ramsize << 20;

    transportSendBegin(MON_HELLO, 5);
    transportSendWord(MON_PROTO_VER);
    sendWord32(MON_SRAM_BASE);
    sendWord32(ramSize);
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
static void cmdReadMem(const uint16_t *p) {
    uint32_t addr = rd32(p, 0);
    uint32_t len = rd32(p, 2);
    volatile const uint8_t *src = (volatile const uint8_t *)addr;

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
static void streamWriteMem(uint16_t frameWords) {
    uint32_t addr = recvU32();   /* 2 words */
    uint32_t nbytes = recvU32(); /* 2 words */
    uint32_t consumed = 4;
    volatile uint8_t *dst = (volatile uint8_t *)addr;

    for (uint16_t w = consumed; w < frameWords; w++) {
        uint16_t word = transportRecvWord();
        uint32_t bi = (uint32_t)(w - consumed) * 2;
        if (bi < nbytes) dst[bi] = (uint8_t)(word & 0xff);
        if (bi + 1 < nbytes) dst[bi + 1] = (uint8_t)(word >> 8);
    }

    int rc = transportRecvEnd();
    if (rc == TRANSPORT_OK) {
        sendAck();
    } else {
        sendError(MON_ECKSUM);
    }
}

/* GET_REGS [] -> REGS [38 x u32, gdb g-packet order]. */
static void cmdGetRegs(void) {
    struct Registers *r = s_ctx;
    transportSendBegin(MON_REGS, 38 * 2);
    for (int i = 0; i < 32; i++) sendWord32(r ? r->GPR.r[i] : 0); /* 0..31 r0..r31 */
    sendWord32(r ? r->SR : 0);                                    /* 32 SR */
    sendWord32(r ? r->lo : 0);                                    /* 33 LO */
    sendWord32(r ? r->hi : 0);                                    /* 34 HI */
    sendWord32(s_badVaddr);                                       /* 35 BadVaddr */
    sendWord32(r ? r->Cause : 0);                                 /* 36 Cause */
    sendWord32(r ? r->returnPC : 0);                              /* 37 PC (EPC) */
    transportSendEnd();
}

/* SET_REG [idx:u16][value:u32] -> ACK | ERROR. idx indexes the REGS table. */
static void cmdSetReg(const uint16_t *p) {
    uint16_t idx = p[0];
    uint32_t val = rd32(p, 1);
    struct Registers *r = s_ctx;

    if (idx > 37) {
        sendError(MON_EBADREG);
        return;
    }
    if (!r) {
        sendError(MON_EBADSTATE);
        return;
    }

    if (idx < 32) {
        if (idx != 0) r->GPR.r[idx] = val; /* r0 stays 0 */
    } else {
        switch (idx) {
            case 32: r->SR = val; break;
            case 33: r->lo = val; break;
            case 34: r->hi = val; break;
            case 35: s_badVaddr = val; break;
            case 36: r->Cause = val; break;
            case 37: r->returnPC = val; break;
        }
    }
    sendAck();
}

/* SET_BP [kind:u16][addr:u32][mask:u32] -> ACK.
   kind: 0 exec, 1 data-read, 2 data-write, 3 data-rw. */
static void cmdSetBp(const uint16_t *p) {
    uint16_t kind = p[0];
    uint32_t addr = rd32(p, 1);
    uint32_t mask = rd32(p, 3);

    if (kind == 0) {
        writeBPC(addr);
        writeBPCM(mask);
        s_dcic |= DCIC_DE | DCIC_PCE | DCIC_TR | DCIC_KD | DCIC_UD;
    } else if (kind <= 3) {
        writeBDA(addr);
        writeBDAM(mask);
        s_watchAddr = addr;
        uint32_t enables = DCIC_DE | DCIC_DAE | DCIC_TR | DCIC_KD | DCIC_UD;
        if (kind == 1) enables |= DCIC_DR;
        if (kind == 2) enables |= DCIC_DW;
        if (kind == 3) enables |= DCIC_DR | DCIC_DW;
        s_dcic |= enables;
    } else {
        sendError(MON_EBADCMD);
        return;
    }
    writeDCIC(s_dcic);
    sendAck();
}

/* CLR_BP [kind:u16] -> ACK. */
static void cmdClrBp(const uint16_t *p) {
    uint16_t kind = p[0];
    if (kind == 0) {
        s_dcic &= ~DCIC_PCE;
    } else {
        s_dcic &= ~(DCIC_DAE | DCIC_DR | DCIC_DW);
    }
    if ((s_dcic & (DCIC_PCE | DCIC_DAE)) == 0) s_dcic = 0; /* drop master enable */
    writeDCIC(s_dcic);
    sendAck();
}

/* RUN [pc:u32][gp:u32][sp:u32] -> ACK, then hand the CPU to the target. Never
   returns (returnFromException does an rfe into pc). */
static void cmdRun(const uint16_t *p) {
    uint32_t pc = rd32(p, 0);
    uint32_t gp = rd32(p, 2);
    uint32_t sp = rd32(p, 4);
    struct Registers *r = currentRegs();

    for (int i = 0; i < 32; i++) r->GPR.r[i] = 0;
    r->GPR.n.gp = gp;
    r->GPR.n.sp = sp;
    r->GPR.n.fp = sp;
    r->returnPC = pc;
    r->hi = 0;
    r->lo = 0;
    r->SR = MON_RUN_SR;
    r->Cause = 0;
    s_ctx = 0; /* running: no halted context */

    sendAck();
    monitorResume();
}

/* CONT [] -> ACK, then resume the saved (possibly SET_REG-modified) context. */
static void cmdCont(void) {
    if (!s_ctx) {
        sendError(MON_EBADSTATE);
        return;
    }
    sendAck();
    monitorResume();
}

/* Dispatch one small HALTED-state command whose payload is already buffered in
   s_cmd. WRITE_MEM/LOAD are NOT here - they stream directly to target memory in
   the loop. RUN/CONT do not return (they resume the target). */
static void dispatchCommand(uint16_t type, const uint16_t *payload) {
    switch (type) {
        case MON_PING: {
            uint16_t ver = MON_PROTO_VER;
            transportSendFrame(MON_PONG, &ver, 1);
            break;
        }
        case MON_READ_MEM: cmdReadMem(payload); break;
        case MON_GET_REGS: cmdGetRegs(); break;
        case MON_SET_REG: cmdSetReg(payload); break;
        case MON_SET_BP: cmdSetBp(payload); break;
        case MON_CLR_BP: cmdClrBp(payload); break;
        case MON_RUN: cmdRun(payload); break;
        case MON_CONT: cmdCont(); break;
        case MON_STOP:
            /* Only meaningful while RUNNING, where it is serviced by the
               interrupt-poll path (v1 seam, below). In HALTED it is a no-op. */
            sendAck();
            break;
        default: sendError(MON_EBADCMD); break;
    }
}

/* Blocking HALTED command loop. Bulk WRITE_MEM/LOAD payloads are decoded
   straight into target memory (no staging); every other command has a small
   fixed payload buffered in s_cmd. Returns only via a resume, which is noreturn,
   so in practice it does not return. */
static __attribute__((noreturn)) void monitorCommandLoop(void) {
    for (;;) {
        uint16_t type;
        uint16_t len;
        transportRecvBegin(&type, &len);

        if (type == MON_WRITE_MEM || type == MON_LOAD) {
            streamWriteMem(len); /* + LOAD extent bookkeeping later */
            continue;
        }

        uint16_t n = (len <= MON_CMD_WORDS) ? len : MON_CMD_WORDS;
        for (uint16_t i = 0; i < n; i++) s_cmd[i] = transportRecvWord();
        for (uint16_t i = n; i < len; i++) transportRecvWord(); /* drain overflow */
        int rc = transportRecvEnd();

        if (rc != TRANSPORT_OK) {
            sendError(MON_ECKSUM);
            continue;
        }
        if (len > MON_CMD_WORDS) {
            sendError(MON_EBADLEN);
            continue;
        }
        dispatchCommand(type, s_cmd);
    }
}

/* Snapshot a stop, tell the host, and drop into the command loop until the host
   resumes. Never returns. */
static __attribute__((noreturn)) void monitorStop(struct Registers *r, uint16_t reason, uint32_t a, uint32_t b) {
    s_ctx = r;
    s_badVaddr = (reason == MON_STOP_FAULT) ? b : 0;
    emitStopped(reason, r->returnPC, a, b);
    monitorCommandLoop();
    __builtin_unreachable();
}

/* ---- exception hook ---- */

/* Priority-0 verifier, prepended ahead of the kernel's syscall verifier so it
   sees break (ExcCode 9) and faults first. It reuses the kernel's context save
   (the frame is already in currentRegs()); both the 0x80 general vector and the
   0x40 cop0-break vector route here. Returns 0 for exceptions it does not own
   (interrupt, syscall) so the rest of the chain runs; when it owns one it never
   returns (it resumes the target or enters the command loop). */
static int monitorVerifier(void) {
    struct Registers *r = currentRegs();
    uint32_t excode = CAUSE_EXCCODE(r->Cause);

    if (excode == EXCCODE_BP) {
        uint32_t insn = *(volatile uint32_t *)(r->returnPC);
        if ((insn & 0x3f) == 0x0d) {
            /* Software `break`: decode break code1, code2 (design section 10). */
            uint32_t code1 = (insn >> 16) & 0x3ff;
            uint32_t code2 = (insn >> 6) & 0x3ff;
            if (code1 == 0 && code2 >= 0x101 && code2 <= 0x107) {
                monitorServicePcdrv(r, code2);
                r->returnPC += 4; /* step past the break; program stays RUNNING */
                monitorResume();
            }
            if (code1 == 4) {
                switch (code2) {
                    case 0: /* exit break (break 4, 0): exit code in $a0. */
                        monitorStop(r, MON_STOP_EXIT, r->GPR.n.a0, 0);
                        break;
                    case 1: /* enter the monitor command loop from (break 4, 1). */
                        STAGE_MARK(9);
                        emitHello();
                        STAGE_MARK(10);
                        monitorCommandLoop();
                        break;
                }
            }
            /* Any other software break -> generic debugger stop. Advance past it
               so a following CONT resumes the instruction after the break. */
            r->returnPC += 4;
            monitorStop(r, MON_STOP_BREAKPOINT, 0, 0);
        } else {
            /* No `break` at EPC: a cop0 hardware breakpoint. Disable the debug
               unit first (design section 10), then report. */
            uint32_t dcic = readDCIC();
            writeDCIC(0);
            s_dcic = 0;
            if (dcic & DCIC_DAE) {
                monitorStop(r, MON_STOP_DATA_WATCH, s_watchAddr, 0);
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

    /* Interrupt (0) / syscall (8) / anything else: not ours. The async STOP
       (host Ctrl-C) poll-on-any-interrupt is a v1 seam - it would peek the word
       channel here and, if a STOP frame is pending, snapshot and halt. Banked
       for now (design section 12); the target must be interruptible to use it. */
    return 0;
}

/* Copy the installed 0x80 general-exception trampoline down to the 0x40 cop0
   break vector so a hardware breakpoint routes through the same handler and
   chain. Nothing is installed at 0x40 by OpenBIOS. */
static void installCop0BreakVector(void) {
    volatile uint32_t *v80 = (volatile uint32_t *)0x80;
    volatile uint32_t *v40 = (volatile uint32_t *)0x40;
    for (int i = 0; i < 4; i++) v40[i] = v80[i];
    syscall_flushCache();
}

static struct HandlerInfo s_monitorHandler = {
    .next = 0,
    .handler = 0,
    .verifier = monitorVerifier,
    .padding = 0,
};

void monitorMain(void) {
    STAGE_MARK(6);
    psxprintf("OpenBIOS Monitor.\n");
    transportInit();
    STAGE_MARK(7);
    s_ctx = 0;
    s_badVaddr = 0;
    s_dcic = 0;
    s_watchAddr = 0;

    /* Own the break/fault path (priority 0, ahead of the syscall verifier) and
       the cop0 break vector, then announce readiness. */
    syscall_sysEnqIntRP(0, &s_monitorHandler);
    installCop0BreakVector();
    STAGE_MARK(8);

    /* Calls into the exception handler to ensure the monitor loop is run from there safely. */
    monitorEnter();
}
