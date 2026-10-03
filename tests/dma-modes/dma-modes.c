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
 * Which sync modes each DMA channel accepts, and what each one does.
 *
 * MASK: per channel, the CHCR/MADR/BCR bits that read back after writing all
 * ones (CHCR bits 24 and 28 left clear so nothing starts).
 *
 * Transfers: every channel is run in sync mode 0 (with and without bit 28),
 * 1 (with and without bit 28), 2 and 3. SPU, MDEC and GPU have a request line
 * the program can turn off (SPUCNT transfer mode, MDEC control bits 29/30,
 * GP1(04h)), so for those the same transfer runs with the request on and off.
 * That shows what a channel waiting on a request that never comes looks like,
 * which is the reference for CDROM (request register BFRD) and PIO.
 *
 * Every transfer is bounded by a poll limit. On timeout the channel is
 * stopped by writing CHCR with bits 24 and 28 clear, and the registers are
 * read again. Each ARM line is printed before the transfer starts, so a hang
 * shows which one.
 *
 * Observables: SPU RAM is read back with a plain slice-mode read; MDEC
 * command 0 copies its low 16 bits to status bits 0-15; GP0(E1h) copies its
 * low 11 bits to GPUSTAT bits 0-10; device-to-RAM arms fill a poisoned buffer.
 * Poison is 00FFFFFEh: as a linked-list header it is a zero-length node
 * pointing outside RAM, so a runaway list stops on a bus error.
 */

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/syscalls/syscalls.h"

#define BUSY 0x01000000u
#define TRIG 0x10000000u
#define POISON 0x00fffffeu
#define LIMIT 400000u
#define IRQ_DMA_BIT 0x8u

#define SPUCNT (*(volatile uint16_t *)0xbf801daa)
#define SPUSTAT (*(volatile uint16_t *)0xbf801dae)
#define SPU_ADDR (*(volatile uint16_t *)0xbf801da6)
#define SPU_TCTRL (*(volatile uint16_t *)0xbf801dac)
#define MDEC_CMD (*(volatile uint32_t *)0xbf801820)
#define MDEC_CTRL (*(volatile uint32_t *)0xbf801824)
#define CD_REG0 (*(volatile uint8_t *)0xbf801800)
#define CD_REG3 (*(volatile uint8_t *)0xbf801803)

static inline uint32_t irqDisable(void) {
    uint32_t sr, n;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    n = sr & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(n));
    return sr;
}
static inline void irqRestore(uint32_t sr) { __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr)); }

static void delay(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++);
}

struct Res {
    int done;
    uint32_t polls, chcr, madr, bcr, dicr, stopChcr, stopMadr, stopBcr;
};

static uint32_t s_dicr0;

static void run(int ch, uint32_t madr, uint32_t bcr, uint32_t chcr, struct Res *r) {
    uint32_t sr = irqDisable();
    uint32_t imask = IMASK;
    IMASK = imask & ~IRQ_DMA_BIT;
    uint32_t en = 8u << (ch * 4);
    DICR = 0x7f800000u | (1u << (16 + ch));
    DPCR |= en;
    DMA_CTRL[ch].MADR = madr;
    DMA_CTRL[ch].BCR = bcr;
    DMA_CTRL[ch].CHCR = chcr;
    uint32_t n = 0;
    while ((DMA_CTRL[ch].CHCR & BUSY) && n < LIMIT) n++;
    r->polls = n;
    r->chcr = DMA_CTRL[ch].CHCR;
    r->madr = DMA_CTRL[ch].MADR;
    r->bcr = DMA_CTRL[ch].BCR;
    r->dicr = DICR;
    r->done = !(r->chcr & BUSY);
    r->stopChcr = r->stopMadr = r->stopBcr = 0;
    if (!r->done) {
        DMA_CTRL[ch].CHCR = chcr & ~(BUSY | TRIG);
        delay(1000);
        r->stopChcr = DMA_CTRL[ch].CHCR;
        r->stopMadr = DMA_CTRL[ch].MADR;
        r->stopBcr = DMA_CTRL[ch].BCR;
        if (r->stopChcr & BUSY) {
            /* Last resort: take the master enable away. */
            DPCR &= ~en;
            delay(1000);
            r->stopChcr = DMA_CTRL[ch].CHCR;
        }
    }
    DPCR &= ~en;
    DICR = 0x7f000000u | s_dicr0;
    IREG = ~IRQ_DMA_BIT;
    IMASK = imask;
    irqRestore(sr);
}

static void armStart(const char *name, int ch, uint32_t chcr) {
    ramsyscall_printf("ARM %-14s ch=%d chcr=%08x ...", name, ch, chcr);
}

static void armEnd(const struct Res *r) {
    if (r->done) {
        ramsyscall_printf(" done polls=%u CHCR=%08x MADR=%08x BCR=%08x DICR=%08x", r->polls, r->chcr, r->madr, r->bcr,
                          r->dicr);
    } else {
        ramsyscall_printf(" TIMEOUT CHCR=%08x MADR=%08x BCR=%08x DICR=%08x stop:CHCR=%08x MADR=%08x BCR=%08x", r->chcr,
                          r->madr, r->bcr, r->dicr, r->stopChcr, r->stopMadr, r->stopBcr);
    }
}

/* ---- buffers ---- */

#define NBUF 64
static uint32_t s_src[NBUF] __attribute__((aligned(16)));
static uint32_t s_dst[NBUF] __attribute__((aligned(16)));
static uint32_t s_list[NBUF] __attribute__((aligned(16)));
static uint32_t s_tmp[NBUF] __attribute__((aligned(16)));

static uint32_t lo24(const void *p) { return ((uint32_t)p) & 0x00ffffffu; }

/*
 * Linked list in s_list: node A at [0] (3 words), node B at [8] (2 words),
 * terminator. Payload words are tag|0..4, so the words that arrive show
 * whether headers were sent along with the payload. BCR is set to BS=4,BA=1
 * by the callers, so a channel that ignores the mode and copies 4 words from
 * MADR delivers the header of A plus its first 3 payload words.
 */
static void buildList(uint32_t tag) {
    for (int i = 0; i < NBUF; i++) s_list[i] = POISON;
    s_list[0] = (3u << 24) | lo24(&s_list[8]);
    s_list[1] = tag | 0;
    s_list[2] = tag | 1;
    s_list[3] = tag | 2;
    s_list[8] = (2u << 24) | 0x00ffffffu;
    s_list[9] = tag | 3;
    s_list[10] = tag | 4;
}

static void fillPoison(uint32_t *b) {
    for (int i = 0; i < NBUF; i++) b[i] = POISON;
}

static void dumpChanged(const uint32_t *b) {
    int n = 0;
    for (int i = 0; i < NBUF; i++)
        if (b[i] != POISON) n++;
    ramsyscall_printf(" changed=%d", n);
    for (int i = 0; i < NBUF; i++)
        if (b[i] != POISON) ramsyscall_printf(" [%d]=%08x", i, b[i]);
}

static void dumpList(void) {
    /* For device-to-RAM linked-list arms: show the list area as it is now. */
    ramsyscall_printf(" list:");
    for (int i = 0; i < 12; i++) ramsyscall_printf(" %08x", s_list[i]);
    int extra = 0;
    for (int i = 12; i < NBUF; i++)
        if (s_list[i] != POISON) extra++;
    ramsyscall_printf(" extra=%d", extra);
}

/* ---- MASK ---- */

static void maskSection(void) {
    uint32_t sr = irqDisable();
    uint32_t dpcr = DPCR;
    DPCR = dpcr & ~0x08888888u;
    uint32_t c[7], c3[7], c28[7], m[7], b[7];
    for (int ch = 0; ch < 7; ch++) {
        DMA_CTRL[ch].CHCR = 0xeeffffffu;
        c[ch] = DMA_CTRL[ch].CHCR;
        DMA_CTRL[ch].CHCR = 0x00000600u;
        c3[ch] = DMA_CTRL[ch].CHCR;
        DMA_CTRL[ch].CHCR = TRIG;
        c28[ch] = DMA_CTRL[ch].CHCR;
        DMA_CTRL[ch].CHCR = 0;
        uint32_t om = DMA_CTRL[ch].MADR, ob = DMA_CTRL[ch].BCR;
        DMA_CTRL[ch].MADR = 0xffffffffu;
        DMA_CTRL[ch].BCR = 0xffffffffu;
        m[ch] = DMA_CTRL[ch].MADR;
        b[ch] = DMA_CTRL[ch].BCR;
        DMA_CTRL[ch].MADR = om;
        DMA_CTRL[ch].BCR = ob;
    }
    DPCR = dpcr;
    irqRestore(sr);
    for (int ch = 0; ch < 7; ch++)
        ramsyscall_printf("MASK ch=%d chcr(eeffffff)=%08x chcr(600)=%08x chcr(10000000)=%08x madr=%08x bcr=%08x\n", ch,
                          c[ch], c3[ch], c28[ch], m[ch], b[ch]);
}

/* ---- SPU (DMA4) ---- */

#define SPU_BASE 0x10000u /* bytes, past the capture buffers */

static int spuMode(unsigned mode) {
    SPUCNT = (SPUCNT & ~0x0030u) | (mode << 4);
    uint32_t n = 0;
    while ((SPUSTAT & 0x0030u) != (mode << 4) && n < 100000) n++;
    return n < 100000;
}

static void spuWait(void) {
    uint32_t n = 0;
    while ((SPUSTAT & 0x0400u) && n < 100000) n++;
}

static void spuAddr(uint32_t bytes) {
    spuMode(0);
    SPU_ADDR = bytes >> 3;
}

static void spuWriteKnown(uint32_t bytes, const uint32_t *src, unsigned words) {
    struct Res r;
    spuAddr(bytes);
    spuMode(2);
    run(4, (uint32_t)src, ((words / 4) << 16) | 4, 0x01000201u, &r);
    spuWait();
    spuMode(0);
}

static void spuReadKnown(uint32_t bytes, uint32_t *dst, unsigned words) {
    struct Res r;
    spuAddr(bytes);
    spuMode(3);
    run(4, (uint32_t)dst, ((words / 4) << 16) | 4, 0x01000200u, &r);
    spuWait();
    spuMode(0);
}

static void spuFill(uint32_t value, int indexed) {
    for (int i = 0; i < NBUF; i++) s_tmp[i] = indexed ? (value | i) : value;
    spuWriteKnown(SPU_BASE, s_tmp, NBUF);
}

static void spuShow(void) {
    for (int i = 0; i < NBUF; i++) s_dst[i] = 0;
    spuReadKnown(SPU_BASE, s_dst, 16);
    ramsyscall_printf(" spu:");
    for (int i = 0; i < 16; i++) ramsyscall_printf(" %08x", s_dst[i]);
}

/* RAM -> SPU. spuMode during the transfer: 2 = request on, 0 = request off. */
static void spuWriteArm(const char *name, uint32_t chcr, uint32_t bcr, unsigned reqMode, int useList) {
    struct Res r;
    spuFill(0x5a5a0000u, 1);
    for (int i = 0; i < NBUF; i++) s_src[i] = 0xa0000000u | i;
    if (useList) buildList(0xb0000000u);
    spuAddr(SPU_BASE);
    spuMode(reqMode);
    armStart(name, 4, chcr);
    run(4, useList ? (uint32_t)s_list : (uint32_t)s_src, bcr, chcr, &r);
    spuWait();
    spuMode(0);
    armEnd(&r);
    spuShow();
    ramsyscall_printf("\n");
}

/* SPU -> RAM. reqMode 3 = request on, 0 = off. spuData is what SPU RAM holds. */
static void spuReadArm(const char *name, uint32_t chcr, uint32_t bcr, unsigned reqMode, uint32_t spuData, int useList) {
    struct Res r;
    spuFill(spuData, 0);
    fillPoison(s_dst);
    if (useList) {
        for (int i = 0; i < NBUF; i++) s_list[i] = POISON;
        s_list[0] = (2u << 24) | lo24(&s_list[8]);
        s_list[8] = (1u << 24) | 0x00ffffffu;
    }
    spuAddr(SPU_BASE);
    spuMode(reqMode);
    armStart(name, 4, chcr);
    run(4, useList ? (uint32_t)s_list : (uint32_t)s_dst, bcr, chcr, &r);
    spuWait();
    spuMode(0);
    armEnd(&r);
    if (useList)
        dumpList();
    else
        dumpChanged(s_dst);
    ramsyscall_printf("\n");
}

/*
 * Drain check for device-to-RAM linked lists: SPU RAM holds indexed words,
 * an optional linked-list read runs first, then a 4-word sync 1 read follows
 * with the transfer address left alone. If the list pulled words out of the
 * SPU, the follow-up read starts past index 0. The control arm skips the list.
 */
static void spuDrainArm(const char *name, int withList) {
    struct Res r;
    spuFill(0x12340000u, 1);
    fillPoison(s_dst);
    for (int i = 0; i < NBUF; i++) s_list[i] = POISON;
    s_list[0] = (2u << 24) | lo24(&s_list[8]);
    s_list[8] = (1u << 24) | 0x00ffffffu;
    spuAddr(SPU_BASE);
    spuMode(3);
    armStart(name, 4, withList ? 0x01000400u : 0x01000200u);
    if (withList) {
        run(4, (uint32_t)s_list, (1u << 16) | 4, 0x01000400u, &r);
        ramsyscall_printf(" list done=%d MADR=%08x", r.done, r.madr);
    }
    run(4, (uint32_t)s_dst, (1u << 16) | 4, 0x01000200u, &r);
    spuWait();
    spuMode(0);
    armEnd(&r);
    dumpChanged(s_dst);
    dumpList();
    ramsyscall_printf("\n");
}

static void spuSection(void) {
    SPUCNT = 0xc000; /* on, unmuted, transfer mode stop */
    SPU_TCTRL = 0x0004;
    delay(10000);

    /* Control: the readback path itself. */
    spuFill(0x5a5a0000u, 1);
    ramsyscall_printf("SPU control fill 5a5a00xx:");
    spuShow();
    ramsyscall_printf("\n");

    const uint32_t s16 = (4u << 16) | 4; /* 4 blocks of 4 words */
    const uint32_t l4 = (1u << 16) | 4;  /* one block of 4, for list arms */
    spuWriteArm("w-s1-req", 0x01000201u, s16, 2, 0);
    spuWriteArm("w-s1-noreq", 0x01000201u, s16, 0, 0);
    spuWriteArm("w-s1t-noreq", 0x11000201u, s16, 0, 0);
    spuWriteArm("w-s1t-req", 0x11000201u, s16, 2, 0);
    spuWriteArm("w-s0-req", 0x01000001u, 16, 2, 0);
    spuWriteArm("w-s0t-req", 0x11000001u, 16, 2, 0);
    spuWriteArm("w-s0-noreq", 0x01000001u, 16, 0, 0);
    spuWriteArm("w-s0t-noreq", 0x11000001u, 16, 0, 0);
    spuWriteArm("w-s2-req", 0x01000401u, l4, 2, 1);
    spuWriteArm("w-s2-noreq", 0x01000401u, l4, 0, 1);
    spuWriteArm("w-s1tx-noreq", 0x31000201u, s16, 0, 0);

    const uint32_t r16 = (4u << 16) | 4;
    spuReadArm("r-s1-req", 0x01000200u, r16, 3, 0x12340000u, 0);
    spuReadArm("r-s1-noreq", 0x01000200u, r16, 0, 0x12340000u, 0);
    spuReadArm("r-s0t-req", 0x11000000u, 16, 3, 0x12340000u, 0);
    spuReadArm("r-s2-term", 0x01000400u, l4, 3, 0x00ffffffu, 1);
    spuReadArm("r-s2-one", 0x01000400u, l4, 3, 0x01ffffffu, 1);
    spuReadArm("r-s2-noreq", 0x01000400u, l4, 0, 0x00ffffffu, 1);
    spuDrainArm("r-drain-ctl", 0);
    spuDrainArm("r-drain-list", 1);
    SPUCNT = 0xc000;
}

/* ---- MDEC (DMA0 in, DMA1 out) ---- */

static void mdecReset(uint32_t ctrl) {
    MDEC_CTRL = 0x80000000u;
    delay(1000);
    MDEC_CTRL = ctrl;
}

static void mdecInArm(const char *name, uint32_t chcr, uint32_t bcr, uint32_t ctrl, int useList) {
    struct Res r;
    /* MDEC(0) words: command 0, low 16 bits copied to status bits 0-15. */
    for (int i = 0; i < NBUF; i++) s_src[i] = 0x1000u + i;
    if (useList) buildList(0x00002000u);
    mdecReset(ctrl);
    uint32_t st0 = MDEC_CTRL;
    armStart(name, 0, chcr);
    run(0, useList ? (uint32_t)s_list : (uint32_t)s_src, bcr, chcr, &r);
    uint32_t st1 = MDEC_CTRL;
    armEnd(&r);
    ramsyscall_printf(" mdecstat %08x->%08x listA=%04x\n", st0, st1, lo24(&s_list[8]) & 0xffff);
}

static void mdecOutArm(const char *name, uint32_t chcr, uint32_t bcr, uint32_t ctrl, int useList) {
    struct Res r;
    fillPoison(s_dst);
    if (useList) {
        for (int i = 0; i < NBUF; i++) s_list[i] = POISON;
        s_list[0] = (2u << 24) | lo24(&s_list[8]);
        s_list[8] = (1u << 24) | 0x00ffffffu;
    }
    mdecReset(ctrl);
    uint32_t st0 = MDEC_CTRL;
    armStart(name, 1, chcr);
    run(1, useList ? (uint32_t)s_list : (uint32_t)s_dst, bcr, chcr, &r);
    uint32_t st1 = MDEC_CTRL;
    armEnd(&r);
    ramsyscall_printf(" mdecstat %08x->%08x", st0, st1);
    if (useList)
        dumpList();
    else
        dumpChanged(s_dst);
    ramsyscall_printf("\n");
}

/*
 * Same as mdecInArm, but the CPU writes the command word to MDEC0 first and
 * DMA0 only carries the parameter words. Status is printed before the
 * command, after it, and after the transfer.
 */
static void mdecCmdArm(const char *name, uint32_t chcr, uint32_t bcr, uint32_t ctrl, uint32_t cmd, int useList) {
    struct Res r;
    for (int i = 0; i < NBUF; i++) s_src[i] = 0x2000u + i;
    if (useList) buildList(0x00003000u);
    mdecReset(ctrl);
    uint32_t st0 = MDEC_CTRL;
    MDEC_CMD = cmd;
    delay(100);
    uint32_t st1 = MDEC_CTRL;
    armStart(name, 0, chcr);
    run(0, useList ? (uint32_t)s_list : (uint32_t)s_src, bcr, chcr, &r);
    uint32_t st2 = MDEC_CTRL;
    armEnd(&r);
    ramsyscall_printf(" mdecstat %08x->%08x->%08x\n", st0, st1, st2);
}

static void mdecSection(void) {
    const uint32_t on = 0x40000000u, off = 0;
    const uint32_t s16 = (4u << 16) | 4, l4 = (1u << 16) | 4;
    mdecInArm("in-s1-req", 0x01000201u, s16, on, 0);
    mdecCmdArm("cmd-s1-req", 0x01000201u, s16, on, 0x00000010u, 0);
    mdecCmdArm("cmd-s1-noreq", 0x01000201u, s16, off, 0x00000010u, 0);
    mdecCmdArm("cmd-s0-req", 0x01000001u, 16, on, 0x00000010u, 0);
    mdecCmdArm("cmd-s2-req", 0x01000401u, l4, on, 0x00000005u, 1);
    mdecCmdArm("cmd-q-s1-req", 0x01000201u, s16, on, 0x40000000u, 0);
    mdecCmdArm("cmd-short-s1", 0x01000201u, s16, on, 0x00000008u, 0);
    mdecCmdArm("cmd-q-s1-noreq", 0x01000201u, s16, off, 0x40000000u, 0);
    mdecCmdArm("cmd-q-s1t-noreq", 0x11000201u, s16, off, 0x40000000u, 0);
    mdecCmdArm("cmd-q-s0-req", 0x01000001u, 16, on, 0x40000000u, 0);
    mdecCmdArm("cmd-q-s0-noreq", 0x01000001u, 16, off, 0x40000000u, 0);
    mdecCmdArm("cmd-q-s2-req", 0x01000401u, l4, on, 0x40000000u, 1);
    mdecCmdArm("cmd-q-s2-noreq", 0x01000401u, l4, off, 0x40000000u, 1);
    mdecInArm("in-s1-noreq", 0x01000201u, s16, off, 0);
    mdecInArm("in-s1t-noreq", 0x11000201u, s16, off, 0);
    mdecInArm("in-s0-req", 0x01000001u, 16, on, 0);
    mdecInArm("in-s0t-req", 0x11000001u, 16, on, 0);
    mdecInArm("in-s0-noreq", 0x01000001u, 16, off, 0);
    mdecInArm("in-s0t-noreq", 0x11000001u, 16, off, 0);
    mdecInArm("in-s2-req", 0x01000401u, l4, on, 1);
    /* Nothing decoded, so the output side has nothing to give. */
    mdecOutArm("out-s1-req", 0x01000200u, s16, 0x20000000u, 0);
    mdecOutArm("out-s1-noreq", 0x01000200u, s16, off, 0);
    mdecOutArm("out-s2-req", 0x01000400u, l4, 0x20000000u, 1);
    mdecOutArm("out-s0t-req", 0x11000000u, 16, 0x20000000u, 0);
    mdecReset(0);
}

/* ---- GPU (DMA2, RAM -> GP0 only) ---- */

static void gpuArm(const char *name, uint32_t chcr, uint32_t bcr, uint32_t dir, int useList) {
    struct Res r;
    for (int i = 0; i < NBUF; i++) s_src[i] = 0xe1000000u | (0x100u + i);
    if (useList) {
        for (int i = 0; i < NBUF; i++) s_list[i] = POISON;
        s_list[0] = (3u << 24) | lo24(&s_list[8]);
        s_list[1] = 0xe1000201u;
        s_list[2] = 0xe1000202u;
        s_list[3] = 0xe1000203u;
        s_list[8] = (2u << 24) | 0x00ffffffu;
        s_list[9] = 0xe1000204u;
        s_list[10] = 0xe1000205u;
    }
    GPU_STATUS = 0x00000000u; /* GP1(00h) reset */
    delay(10000);
    GPU_DATA = 0xe1000000u;
    GPU_STATUS = 0x04000000u | dir;
    uint32_t st0 = GPU_STATUS;
    armStart(name, 2, chcr);
    run(2, useList ? (uint32_t)s_list : (uint32_t)s_src, bcr, chcr, &r);
    delay(10000);
    uint32_t st1 = GPU_STATUS;
    armEnd(&r);
    ramsyscall_printf(" gpustat %08x->%08x\n", st0, st1);
}

static void gpuSection(void) {
    const uint32_t s16 = (4u << 16) | 4, l4 = (1u << 16) | 4;
    gpuArm("g-s1-req", 0x01000201u, s16, 2, 0);
    gpuArm("g-s1-noreq", 0x01000201u, s16, 0, 0);
    gpuArm("g-s1t-noreq", 0x11000201u, s16, 0, 0);
    gpuArm("g-s0-req", 0x01000001u, 16, 2, 0);
    gpuArm("g-s0t-req", 0x11000001u, 16, 2, 0);
    gpuArm("g-s0-noreq", 0x01000001u, 16, 0, 0);
    gpuArm("g-s0t-noreq", 0x11000001u, 16, 0, 0);
    gpuArm("g-s2-req", 0x01000401u, l4, 2, 1);
    gpuArm("g-s2-noreq", 0x01000401u, l4, 0, 1);
    GPU_STATUS = 0x00000000u;
}

/* ---- CDROM (DMA3, device -> RAM) ---- */

static void cdArm(const char *name, uint32_t chcr, uint32_t bcr, uint8_t req, int useList) {
    struct Res r;
    fillPoison(s_dst);
    if (useList) {
        for (int i = 0; i < NBUF; i++) s_list[i] = POISON;
        s_list[0] = (2u << 24) | lo24(&s_list[8]);
        s_list[8] = (1u << 24) | 0x00ffffffu;
    }
    CD_REG0 = 0;
    CD_REG3 = req;
    uint8_t st0 = CD_REG0;
    armStart(name, 3, chcr);
    run(3, useList ? (uint32_t)s_list : (uint32_t)s_dst, bcr, chcr, &r);
    uint8_t st1 = CD_REG0;
    CD_REG0 = 0;
    CD_REG3 = 0;
    armEnd(&r);
    ramsyscall_printf(" cdstat %02x->%02x", st0, st1);
    if (useList)
        dumpList();
    else
        dumpChanged(s_dst);
    ramsyscall_printf("\n");
}

static void cdWriteArm(const char *name, uint32_t chcr, uint32_t bcr, int useList) {
    struct Res r;
    for (int i = 0; i < NBUF; i++) s_src[i] = 0;
    if (useList) buildList(0);
    CD_REG0 = 0;
    uint8_t st0 = CD_REG0;
    armStart(name, 3, chcr);
    run(3, useList ? (uint32_t)s_list : (uint32_t)s_src, bcr, chcr, &r);
    uint8_t st1 = CD_REG0;
    CD_REG0 = 0;
    CD_REG3 = 0x40; /* clear parameter FIFO */
    CD_REG3 = 0;
    armEnd(&r);
    ramsyscall_printf(" cdstat %02x->%02x\n", st0, st1);
}

static void cdSection(void) {
    const uint32_t s16 = (4u << 16) | 4, l4 = (1u << 16) | 4;
    cdArm("s0t-nobfrd", 0x11000000u, 16, 0x00, 0);
    cdArm("s0-nobfrd", 0x01000000u, 16, 0x00, 0);
    cdArm("s1-nobfrd", 0x01000200u, s16, 0x00, 0);
    cdArm("s1t-nobfrd", 0x11000200u, s16, 0x00, 0);
    cdArm("s2-nobfrd", 0x01000400u, l4, 0x00, 1);
    cdArm("s0t-bfrd", 0x11000000u, 16, 0x80, 0);
    cdArm("s0-bfrd", 0x01000000u, 16, 0x80, 0);
    cdArm("s1-bfrd", 0x01000200u, s16, 0x80, 0);
    cdArm("s1t-bfrd", 0x11000200u, s16, 0x80, 0);
    cdArm("s2-bfrd", 0x01000400u, l4, 0x80, 1);
    cdArm("s2t-nobfrd", 0x11000400u, l4, 0x00, 1);
    cdArm("s2t-bfrd", 0x11000400u, l4, 0x80, 1);
    cdWriteArm("w-s0t", 0x11000001u, 16, 0);
    cdWriteArm("w-s0", 0x01000001u, 16, 0);
    cdWriteArm("w-s1", 0x01000201u, s16, 0);
    cdWriteArm("w-s1t", 0x11000201u, s16, 0);
    cdWriteArm("w-s2", 0x01000401u, l4, 1);
    cdWriteArm("w-s2t", 0x11000401u, l4, 1);
}

/* ---- PIO (DMA5, device -> RAM only) ---- */

static void pioArm(const char *name, uint32_t chcr, uint32_t bcr, int useList) {
    struct Res r;
    fillPoison(s_dst);
    if (useList) {
        for (int i = 0; i < NBUF; i++) s_list[i] = POISON;
        s_list[0] = (2u << 24) | lo24(&s_list[8]);
        s_list[8] = (1u << 24) | 0x00ffffffu;
    }
    armStart(name, 5, chcr);
    run(5, useList ? (uint32_t)s_list : (uint32_t)s_dst, bcr, chcr, &r);
    armEnd(&r);
    if (useList)
        dumpList();
    else
        dumpChanged(s_dst);
    ramsyscall_printf("\n");
}

static void pioWriteArm(const char *name, uint32_t chcr, uint32_t bcr, int useList) {
    struct Res r;
    for (int i = 0; i < NBUF; i++) s_src[i] = 0;
    if (useList) buildList(0);
    armStart(name, 5, chcr);
    run(5, useList ? (uint32_t)s_list : (uint32_t)s_src, bcr, chcr, &r);
    armEnd(&r);
    ramsyscall_printf("\n");
}

static void pioSection(void) {
    const uint32_t s16 = (4u << 16) | 4, l4 = (1u << 16) | 4;
    pioArm("p-s0t", 0x11000000u, 16, 0);
    pioArm("p-s0", 0x01000000u, 16, 0);
    pioArm("p-s1", 0x01000200u, s16, 0);
    pioArm("p-s1t", 0x11000200u, s16, 0);
    pioArm("p-s2", 0x01000400u, l4, 1);
    pioArm("p-s2t", 0x11000400u, l4, 1);
    pioWriteArm("pw-s0t", 0x11000001u, 16, 0);
    pioWriteArm("pw-s0", 0x01000001u, 16, 0);
    pioWriteArm("pw-s1", 0x01000201u, s16, 0);
    pioWriteArm("pw-s1t", 0x11000201u, s16, 0);
    pioWriteArm("pw-s2", 0x01000401u, l4, 1);
    pioWriteArm("pw-s2t", 0x11000401u, l4, 1);
}

#ifdef MODE3_ARM
/*
 * One sync-3 transfer per executable, because the first one tried (SPU,
 * request on, MADR at a linked list) stopped the console with no further
 * serial output. MODE3_ARM selects:
 *   1 SPU, request on,  MADR at 64 words of 00FFFFFFh
 *   2 SPU, request off, MADR at 64 words of 00FFFFFFh
 *   3 SPU, request on,  MADR at plain data, BCR 4x4
 *   4 SPU, request off, MADR at plain data, BCR 4x4
 *   5 GPU, request on (GP1(04h)=2), MADR at E1h words, BCR 4x4
 *   6 SPU->RAM, request on, MADR at 64 words of 00FFFFFFh
 *   8 not sync 3: linked list with bit 8 set, SPU, request on, MADR at a list
 *   9 not sync 3: linked list + bit 28, RAM->SPU, request off, MADR at a list
 *  10 not sync 3: linked list + bit 28, SPU->RAM, request off, MADR at 00FFFFFFh words
 *   7 not sync 3: sync 1 with bit 8 set, SPU, request on, plain data, BCR 4x4
 */
static void mode3(void) {
    struct Res r;
    int arm = MODE3_ARM;
    uint32_t buf = (uint32_t)s_list;
    for (int i = 0; i < NBUF; i++) s_list[i] = 0x00ffffffu;
    for (int i = 0; i < NBUF; i++) s_src[i] = 0xa0000000u | i;
    uint32_t bcr = (1u << 16) | 4, chcr = 0x01000601u;
    int ch = 4;
    if (arm == 3 || arm == 4) buf = (uint32_t)s_src, bcr = (4u << 16) | 4;
    if (arm == 6) chcr = 0x01000600u;
    if (arm == 8) {
        chcr = 0x01000501u;
        buildList(0xb0000000u);
    }
    if (arm == 9) {
        chcr = 0x11000401u;
        buildList(0xb0000000u);
    }
    if (arm == 10) chcr = 0x11000400u;
    if (arm == 7) chcr = 0x01000301u, buf = (uint32_t)s_src, bcr = (4u << 16) | 4;
    if (arm == 5) {
        ch = 2;
        for (int i = 0; i < NBUF; i++) s_src[i] = 0xe1000000u | (0x100u + i);
        buf = (uint32_t)s_src, bcr = (4u << 16) | 4;
        GPU_STATUS = 0;
        delay(10000);
        GPU_STATUS = 0x04000002u;
    } else {
        SPUCNT = 0xc000;
        SPU_TCTRL = 0x0004;
        delay(10000);
        spuFill(0x5a5a0000u, 1);
        spuAddr(SPU_BASE);
        spuMode((arm == 2 || arm == 4 || arm == 9 || arm == 10) ? 0 : (arm == 6 ? 3 : 2));
    }
    ramsyscall_printf("MODE3 arm=%d ch=%d madr=%08x bcr=%08x", arm, ch, buf, bcr);
    armStart("mode3", ch, chcr);
    run(ch, buf, bcr, chcr, &r);
    armEnd(&r);
    if (ch == 4) {
        spuWait();
        spuMode(0);
        spuShow();
    } else {
        ramsyscall_printf(" gpustat=%08x", GPU_STATUS);
    }
    ramsyscall_printf("\n");
}
#endif

int main(void) {
    irqDisable(); /* Unirom's SIO1 TX does not need interrupts */
    s_dicr0 = DICR & 0x00ff807fu;
    ramsyscall_printf("DMAMODES-START dpcr=%08x dicr=%08x\n", DPCR, DICR);
#ifdef MODE3_ARM
    mode3();
    ramsyscall_printf("DMAMODES-DONE\n");
    while (1) __asm__ __volatile__("");
#endif
    maskSection();
    ramsyscall_printf("SECTION spu\n");
    spuSection();
    ramsyscall_printf("SECTION mdec\n");
    mdecSection();
    ramsyscall_printf("SECTION gpu\n");
    gpuSection();
    ramsyscall_printf("SECTION cd\n");
    cdSection();
    ramsyscall_printf("SECTION pio\n");
    pioSection();
    ramsyscall_printf("DMAMODES-DONE\n");
    while (1) __asm__ __volatile__("");
    return 0;
}
