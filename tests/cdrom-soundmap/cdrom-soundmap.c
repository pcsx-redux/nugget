/*
Probe for the CD-ROM decoder's sound map feature: XA-ADPCM sectors uploaded
by the CPU through WRDATA instead of read from disc.

Every arm fills the SPU CD capture area with a marker, uploads square-wave
ADPCM blocks with a given HCHPCTL value, then reads the capture area back.
A marker that survives means the capture engine never ran; zeros mean it ran
and heard nothing; large samples mean the decoder played the uploaded data.
*/

#include <stdint.h>

#include "common/hardware/cdrom.h"
#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#define printf ramsyscall_printf

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)

#define HSTS_ADPBUSY 0x04
#define HSTS_DRQSTS 0x40
#define HSTS_BUSYSTS 0x80
#define HINT_BFEMPT 0x08
#define HINT_BFWRDY 0x10

#define MARKER 0x5a5a

static uint8_t s_block[0x900];
static uint16_t s_capture[0x800 / 2];
static uint16_t s_fill[0x800 / 2];

static void spu_dma(uint32_t spuAddr, void *buf, uint32_t bytes, int isRead) {
    const uint16_t tsa = spuAddr >> 3;
    const uint16_t mode = isRead ? 0x30 : 0x20;
    SPU_RAM_DTA = tsa;
    for (int i = 0; i < 0xf01 && SPU_RAM_DTA != tsa; i++);
    SPU_CTRL = (SPU_CTRL & ~0x30) | mode;
    for (volatile int i = 0; i < 60; i++);
    for (int i = 0; i < 0xf01 && (SPU_STATUS & 0x30) != mode; i++);
    SPU_DELAY = (SPU_DELAY & 0xf0ffffff) | (isRead ? 0x22000000 : 0x20000000);
    DMA_CTRL[DMA_SPU].MADR = (uint32_t)buf & 0x1fffffff;
    DMA_CTRL[DMA_SPU].BCR = ((bytes >> 6) << 16) | 0x10;
    DMA_CTRL[DMA_SPU].CHCR = isRead ? 0x01000200 : 0x01000201;
    while (DMA_CTRL[DMA_SPU].CHCR & 0x01000000);
    SPU_CTRL = SPU_CTRL & ~0x30;
    for (volatile int i = 0; i < 60; i++);
}

static void busy_wait(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++);
}

static uint8_t hintsts(void) {
    CDROM_REG0 = 1;
    return CDROM_REG3 & 0x1f;
}

static void hclrctl(uint8_t v) {
    CDROM_REG0 = 1;
    CDROM_REG3 = v;
}

// Send a parameterless command and poll for its first response.
static void cd_command(uint8_t cmd, const char *name) {
    hclrctl(0x1f);
    while (CDROM_REG0 & HSTS_BUSYSTS);
    CDROM_REG0 = 0;
    CDROM_REG1 = cmd;
    unsigned i;
    uint8_t irq = 0;
    for (i = 0; i < 2000000; i++) {
        irq = hintsts() & 7;
        if (irq) break;
    }
    uint8_t stat = 0xff;
    if (irq) stat = CDROM_REG1;
    printf("CMD %s int=%d stat=%02x\n", name, irq, stat);
    hclrctl(0x07);
    // Swallow a possible second response (INT2) so it cannot leak into an arm.
    for (i = 0; i < 400000; i++) {
        if (hintsts() & 7) {
            printf("CMD %s second int=%d\n", name, hintsts() & 7);
            hclrctl(0x07);
            break;
        }
    }
}

// Mono, 37800Hz, 4-bit: 18 sound groups of 128 bytes. Filter 0 shift 0, so
// each nibble decodes to nibble << 12 exactly. Square wave of period 28
// samples at +/-0x7000.
static void build_block(void) {
    for (int g = 0; g < 18; g++) {
        uint8_t *sg = s_block + g * 128;
        for (int i = 0; i < 16; i++) sg[i] = 0x00;
        for (int w = 0; w < 28; w++) {
            for (int b = 0; b < 4; b++) {
                // byte b of word w carries blocks 2b (low) and 2b+1 (high)
                uint8_t lo = (w / 14) & 1 ? 0x9 : 0x7;
                uint8_t hi = lo;
                sg[16 + w * 4 + b] = lo | (hi << 4);
            }
        }
    }
}

struct result {
    unsigned drqTimeouts;
    unsigned drqSeen;
    unsigned drqMiss;
    unsigned bfwrdySeen;
    unsigned adpbusySeen;
    uint8_t hintAfterFirst;
};

// Wait on DRQSTS for the first byte only if it never showed up before; once it
// has timed out, write the rest blind so an arm stays inside the run budget.
static void write_block(struct result *r) {
    CDROM_REG0 = 1;
    for (int i = 0; i < 0x900; i++) {
        unsigned limit = (i == 0 || r->drqMiss < 16) ? 20000 : 0;
        unsigned t;
        for (t = 0; t < limit; t++)
            if (CDROM_REG0 & HSTS_DRQSTS) break;
        if (limit && t < limit) {
            r->drqSeen++;
            r->drqMiss = 0;
        } else {
            r->drqTimeouts++;
            r->drqMiss++;
        }
        CDROM_REG0 = 1;
        CDROM_REG1 = s_block[i];
    }
}

static void analyze(const char *arm, int ch, const uint16_t *p) {
    int16_t mn = 32767, mx = -32768;
    unsigned markers = 0, zeros = 0, loud = 0;
    for (int i = 0; i < 512; i++) {
        if (p[i] == MARKER) markers++;
        int16_t s = (int16_t)p[i];
        if (s == 0) zeros++;
        if (s > 0x2000 || s < -0x2000) loud++;
        if (s < mn) mn = s;
        if (s > mx) mx = s;
    }
    printf("CAP %s %c min=%d max=%d loud=%u zeros=%u markers=%u\n", arm,
           ch ? 'R' : 'L', mn, mx, loud, zeros, markers);
}

static void run_arm(const char *arm, uint8_t hchpctl, int upload, int rearm) {
    struct result r = {0};
    printf("BEGIN %s\n", arm);

    // reset sound map state
    hclrctl(0x20 | 0x1f);
    CDROM_REG0 = 0;
    CDROM_REG3 = 0;

    for (int i = 0; i < 0x400; i++) s_fill[i] = MARKER;
    spu_dma(0x000, s_fill, 0x800, 0);

    // CI: mono, 37800Hz, 4-bit, no emphasis
    CDROM_REG0 = 2;
    CDROM_REG1 = 0x00;
    // L->L and R->R full, cross terms off, then unmute and apply
    CDROM_REG0 = 2;
    CDROM_REG2 = 0x80;
    CDROM_REG3 = 0x00;
    CDROM_REG0 = 3;
    CDROM_REG1 = 0x80;
    CDROM_REG2 = 0x00;
    CDROM_REG3 = 0x20;

    printf("  pre hsts=%02x hint=%02x\n", CDROM_REG0, hintsts());
    CDROM_REG0 = 0;
    CDROM_REG3 = hchpctl;
    printf("  post-hchpctl hsts=%02x hint=%02x\n", CDROM_REG0, hintsts());

    int captured = 0;
    if (upload) {
        for (int blk = 0; blk < 8; blk++) {
            if (blk > 0) {
                unsigned t;
                for (t = 0; t < 200000; t++) {
                    if (CDROM_REG0 & HSTS_ADPBUSY) r.adpbusySeen++;
                    if (hintsts() & HINT_BFWRDY) break;
                }
                if (t < 200000) r.bfwrdySeen++;
                hclrctl(HINT_BFWRDY);
            }
            if (rearm && blk > 0) {
                CDROM_REG0 = 0;
                CDROM_REG3 = hchpctl;
            }
            write_block(&r);
            if (blk == 0) r.hintAfterFirst = hintsts();
            busy_wait(20000);
            spu_dma(0x000, s_capture, 0x800, 1);
            captured = 1;
            unsigned loud = 0;
            for (int i = 0; i < 0x400; i++) {
                int16_t v = (int16_t)s_capture[i];
                if (v > 0x2000 || v < -0x2000) loud++;
            }
            printf("  blk %d hsts=%02x hint=%02x drqSeen=%u loud=%u\n", blk, CDROM_REG0, hintsts(), r.drqSeen, loud);
        }
    } else {
        busy_wait(3000000);
    }
    if (!captured) spu_dma(0x000, s_capture, 0x800, 1);

    unsigned t;
    for (t = 0; t < 200000; t++)
        if (hintsts() & HINT_BFEMPT) break;

    printf("ARM %s hchpctl=%02x upload=%d drqSeen=%u drqTimeouts=%u bfwrdy=%u/7 adpbusy=%u hintAfterFirst=%02x bfempt=%s\n",
           arm, hchpctl, upload, r.drqSeen, r.drqTimeouts, r.bfwrdySeen, r.adpbusySeen, r.hintAfterFirst,
           t < 200000 ? "yes" : "no");
    analyze(arm, 0, s_capture);
    analyze(arm, 1, s_capture + 0x200);

    hclrctl(0x20 | 0x1f);
    CDROM_REG0 = 0;
    CDROM_REG3 = 0;
}

int main(void) {
    IMASK &= ~IRQ_CDROM;
    DPCR |= 0x000b0000;

    printf("SOUNDMAP probe start\n");
    build_block();

    SPU_CTRL = 0;
    busy_wait(10000);
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_VOL_CD_LEFT = 0x7fff;
    SPU_VOL_CD_RIGHT = 0x7fff;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_RAM_DTC = 4;
    SPU_CTRL = 0x8000 | 0x0001;

    CDROM_REG0 = 1;
    CDROM_REG2 = 0x00;  // HINTMSK: we poll, no IRQs
    hclrctl(0x1f);

    cd_command(0x01, "Getstat");

    // Test 19h,20h: controller firmware date/version, 4 result bytes.
    hclrctl(0x1f);
    while (CDROM_REG0 & HSTS_BUSYSTS);
    CDROM_REG0 = 0;
    CDROM_REG2 = 0x20;
    CDROM_REG1 = 0x19;
    {
        uint8_t irq = 0;
        for (unsigned i = 0; i < 2000000 && !irq; i++) irq = hintsts() & 7;
        printf("VERSION int=%d", irq);
        CDROM_REG0 = 1;
        for (int i = 0; i < 4; i++) printf(" %02x", CDROM_REG1);
        printf("\n");
        hclrctl(0x07);
    }

    run_arm("none", 0x00, 0, 0);
    run_arm("smen+bfwr", 0x60, 1, 0);
    run_arm("smen+bfwr-rearm", 0x60, 1, 1);
    run_arm("bfwr-rearm", 0x40, 1, 1);

    cd_command(0x0c, "Demute");

    run_arm("demuted-smen+bfwr-rearm", 0x60, 1, 1);
    run_arm("demuted-smen+bfrd+bfwr-rearm", 0xe0, 1, 1);

    printf("SOUNDMAP probe done\n");
    return 0;
}
