/*
 * SPU IRQ9 is one-shot until acknowledged.
 *
 * Voice 1 loops an eight-block sample with IRQA on block 4, so the decoder
 * passes IRQA once per lap, hundreds of times per window. I_MASK bit 9 is off
 * and I_STAT bit 9 is polled and acknowledged on the CPU side each time it
 * sets, so every count is a fresh edge from the SPU. SPUSTAT bit 11 flips are
 * counted beside it as an SPU-side clock, to show the window held many laps.
 *
 * Measured on SCPH-5501: the first match sets SPUSTAT.6 and raises IRQ9, and
 * SPUCNT.6 stays set. With the flag set and the enable still 1, no further
 * match raises IRQ9, and neither does rewriting SPUCNT with bit 6 = 1. Writing
 * bit 6 = 0 clears the flag; matches while it is 0 are not remembered. Setting
 * it to 1 again, IRQA untouched, fires on the next pass.
 */

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/pcsxhw.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)

#define SPU_UPLOAD_ADDR 0x1080u
#define SPU_IRQ_BYTE_ADDR (SPU_UPLOAD_ADDR + 4 * 16)

#define SPU_CTRL_ENABLE 0x8000u
#define SPU_CTRL_UNMUTE 0x4000u
#define SPU_CTRL_IRQ_ENABLE 0x0040u
#define SPU_STATUS_IRQ_FLAG 0x0040u

#define IRQ9 0x0200u

/* One lap of the sample is 224 samples, about 5 ms at pitch 1000h. A live
   IRQA matches early; only a dead one spends the whole budget. */
#define IRQ_POLL_LIMIT 4000000u
/* Reads per watch window: about 3 s on silicon, several hundred laps. */
#define WINDOW 3000000u
/* Fewer bit-11 flips than this and the window did not hold enough laps to
   mean anything (each flip is 256 samples, a lap is 224). */
#define MIN_FLIPS 8u

static uint8_t s_sample[128] __attribute__((aligned(4)));
static int s_failures;

static void make_sample(void) {
    for (int block = 0; block < 8; block++) {
        uint8_t *p = &s_sample[block * 16];
        p[0] = 0x00;
        p[1] = 0x00;
        if (block == 0) p[1] = 0x06; /* loop start */
        if (block == 7) p[1] = 0x03; /* loop end + repeat: this never stops */
        uint8_t nybble = (uint8_t)((block + 1) & 7);
        uint8_t packed = (uint8_t)(nybble | (nybble << 4));
        for (int i = 2; i < 16; i++) p[i] = packed;
    }
}

static void spu_dma_write(uint32_t spuByteAddr, const void *src, uint32_t bytes) {
    const uint16_t tsa = (uint16_t)(spuByteAddr >> 3);
    SPU_RAM_DTA = tsa;
    for (int i = 0; i < 0xF01 && (SPU_RAM_DTA & 0xffff) != tsa; i++);
    SPU_CTRL = (SPU_CTRL & ~0x0030) | 0x0020;
    for (volatile int i = 0; i < 60; i++);
    for (int i = 0; i < 0xF01 && (SPU_STATUS & 0x30) != 0x0020; i++);
    SPU_DELAY = (SPU_DELAY & 0xf0ffffff) | 0x20000000;
    uint32_t blocks = (bytes >> 6) + ((bytes & 0x3f) ? 1 : 0);
    DMA_CTRL[DMA_SPU].MADR = (uint32_t)src & 0x1fffffff;
    DMA_CTRL[DMA_SPU].BCR = (blocks << 16) | 0x10;
    DMA_CTRL[DMA_SPU].CHCR = 0x01000201;
    for (int i = 0; i < 0x100000 && (DMA_CTRL[DMA_SPU].CHCR & 0x01000000) != 0; i++);
    /* Leaving DMA mode as soon as CHCR goes idle loses the tail of the write on
       hardware. SPUSTAT bit 7 sets once the data has landed. The bound covers
       emulators that never set it. */
    for (unsigned i = 0; i < 65536 && (SPU_STATUS & 0x0080) == 0; i++);
    SPU_CTRL = (SPU_CTRL & ~0x0030);
    for (volatile int i = 0; i < 60; i++);
}

static uint32_t irq_poll(uint32_t limit) {
    for (uint32_t i = 0; i < limit; i++) {
        if (SPU_STATUS & SPU_STATUS_IRQ_FLAG) return i + 1;
        __asm__ volatile("");
    }
    return 0;
}

typedef struct {
    uint32_t cpuEdges;
    uint32_t bit11Flips;
    uint16_t statEnd;
    uint16_t ctrlEnd;
} Window;

static void watch(Window *w) {
    uint32_t edges = 0, flips = 0;
    uint16_t last = SPU_STATUS & 0x0800;
    for (uint32_t i = 0; i < WINDOW; i++) {
        if (IREG & IRQ9) {
            edges++;
            IREG = ~IRQ9;
        }
        uint16_t b = SPU_STATUS & 0x0800;
        if (b != last) {
            flips++;
            last = b;
        }
    }
    w->cpuEdges = edges;
    w->bit11Flips = flips;
    w->statEnd = SPU_STATUS & 0xffff;
    w->ctrlEnd = SPU_CTRL & 0xffff;
}

static void check(int ok, const char *what) {
    if (ok) return;
    ramsyscall_printf("SPUIRQREFIRE: FAIL - %s\n", what);
    s_failures++;
}

static void check_window(const char *name, const Window *w, int flagExpected) {
    ramsyscall_printf("SPUIRQREFIRE: %s edges=%lu flips=%lu stat=%04x ctrl=%04x\n", name, w->cpuEdges, w->bit11Flips,
                      w->statEnd, w->ctrlEnd);
    check(w->cpuEdges == 0, "IRQ9 raised again inside the window");
    check(w->bit11Flips >= MIN_FLIPS, "SPUSTAT bit 11 barely moved: the window held too few laps");
    check(((w->statEnd & SPU_STATUS_IRQ_FLAG) != 0) == flagExpected, "SPUSTAT.6 at the end of the window");
}

int main() {
    ramsyscall_printf("SPUIRQREFIRE: start\n");
    const uint32_t savedMask = IMASK;
    IMASK = savedMask & ~IRQ9;

    DPCR |= 0x000b0000;
    SPU_CTRL = 0;
    for (volatile int i = 0; i < 60; i++);
    SPU_RAM_DTC = 4;
    for (volatile int i = 0; i < 60; i++);
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_IRQ_ADDR = 0;
    for (volatile int i = 0; i < 60; i++);
    SPU_CTRL = SPU_CTRL_ENABLE;
    for (volatile int i = 0; i < 60; i++);

    make_sample();
    spu_dma_write(SPU_UPLOAD_ADDR, s_sample, sizeof(s_sample));

    SPU_VOICES[1].sampleStartAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[1].sampleRate = 0x1000;
    SPU_VOICES[1].adsrLo = 0x000f;
    SPU_VOICES[1].adsrHi = 0x0000;
    SPU_VOICES[1].volumeLeft = 0;
    SPU_VOICES[1].volumeRight = 0;
    SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_UNMUTE;
    for (volatile int i = 0; i < 60; i++);

    IREG = ~IRQ9;
    SPU_IRQ_ADDR = (uint16_t)(SPU_IRQ_BYTE_ADDR >> 3);
    SPU_CTRL = (SPU_CTRL & ~0x0030) | SPU_CTRL_ENABLE | SPU_CTRL_UNMUTE | SPU_CTRL_IRQ_ENABLE;
    for (volatile int i = 0; i < 120; i++);
    SPU_KEY_ON_LOW = 1u << 1;
    SPU_KEY_ON_HIGH = 0;

    /* First match: flag and IRQ9 set, enable left alone. */
    const uint32_t pollsA = irq_poll(IRQ_POLL_LIMIT);
    for (volatile int i = 0; i < 200; i++);
    const uint32_t istatA = IREG & IRQ9;
    const uint16_t ctrlA = SPU_CTRL & 0xffff;
    ramsyscall_printf("SPUIRQREFIRE: first polls=%lu istat=%lu stat=%04x ctrl=%04x\n", pollsA, istatA,
                      (unsigned)(SPU_STATUS & 0xffff), (unsigned)ctrlA);
    check(pollsA != 0, "IRQA never matched");
    check(istatA != 0, "the first match did not raise IRQ9");
    check((ctrlA & SPU_CTRL_IRQ_ENABLE) != 0, "the first match cleared SPUCNT.6");
    IREG = ~IRQ9;

    /* Flag set, enable 1, nothing acknowledged, the voice keeps lapping IRQA. */
    Window w;
    watch(&w);
    check_window("flag-set-enabled", &w, 1);

    /* Writing SPUCNT with bit 6 still 1 is not an acknowledge and re-arms nothing. */
    SPU_CTRL = SPU_CTRL | SPU_CTRL_IRQ_ENABLE;
    watch(&w);
    check_window("rewrite-enable-1", &w, 1);

    /* SPUSTAT is read-only. Writing zeroes must not clear the flag: if it did, the
       next lap would latch it again and raise a fresh IRQ9, which the window counts.
       Writing ones must not set anything. Bit 11 moves on its own and is ignored. */
    {
        static const uint16_t vals[2] = {0x0000, 0xffff};
        for (int k = 0; k < 2; k++) {
            IREG = ~IRQ9;
            const uint16_t before = SPU_STATUS & 0xffff;
            SPU_STATUS = vals[k];
            watch(&w);
            ramsyscall_printf("SPUIRQREFIRE: stat-write %04x before=%04x\n", (unsigned)vals[k], (unsigned)before);
            check_window(k ? "after-stat-ffff" : "after-stat-0000", &w, 1);
            check((w.statEnd & 0xf7ff) == (before & 0xf7ff), "writing SPUSTAT changed a bit other than 11");
        }
    }

    /* Bit 6 = 0 acknowledges; matches while disabled leave no trace. */
    SPU_CTRL = SPU_CTRL & ~SPU_CTRL_IRQ_ENABLE;
    for (volatile int i = 0; i < 200; i++);
    const uint16_t statAck = SPU_STATUS & 0xffff;
    ramsyscall_printf("SPUIRQREFIRE: ack stat=%04x\n", (unsigned)statAck);
    check((statAck & SPU_STATUS_IRQ_FLAG) == 0, "writing SPUCNT.6=0 did not clear SPUSTAT.6");
    IREG = ~IRQ9;
    watch(&w);
    check_window("disabled", &w, 0);

    /* Enable again, IRQA untouched: the next pass fires. */
    IREG = ~IRQ9;
    SPU_CTRL = SPU_CTRL | SPU_CTRL_IRQ_ENABLE;
    const uint32_t pollsE = irq_poll(IRQ_POLL_LIMIT);
    for (volatile int i = 0; i < 200; i++);
    const uint32_t istatE = IREG & IRQ9;
    const uint16_t ctrlE = SPU_CTRL & 0xffff;
    ramsyscall_printf("SPUIRQREFIRE: rearm polls=%lu istat=%lu stat=%04x ctrl=%04x\n", pollsE, istatE,
                      (unsigned)(SPU_STATUS & 0xffff), (unsigned)ctrlE);
    check(pollsE != 0, "re-enabling did not fire on a later pass");
    check(istatE != 0, "the re-enabled match did not raise IRQ9");
    check((ctrlE & SPU_CTRL_IRQ_ENABLE) != 0, "the re-enabled match cleared SPUCNT.6");
    IREG = ~IRQ9;

    /* And it is one-shot again. */
    watch(&w);
    check_window("after-rearm", &w, 1);

    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_CTRL = 0;
    IREG = ~IRQ9;
    IMASK = savedMask;

    ramsyscall_printf("SPUIRQREFIRE: %s (%d failures)\n", s_failures ? "FAILURE" : "PASS", s_failures);
    return s_failures ? 1 : 0;
}
