/*
 * Voice volume SWEEP mode (VOLL/VOLR bit 15) hardware characterisation, read
 * through the per-voice current-volume register VOLXL (1F801E00h+N*4).
 *
 * psx-spx documents the sweep field layout (mode, direction, phase, shift,
 * step) but marks the negative-phase behaviour "not yet tested". pcsx-redux
 * collapses sweep to two constants applied instantly. This traces VOLXL once
 * per full SPUSTAT bit-11 period (512 output samples) for each case so the rate,
 * shape and end point can be read off silicon, and checks each trace against
 * an SCPH-1001 golden (exit 1 on any mismatch).
 *
 * Case "direct" is the instrument control: a direct-mode write of v must read
 * back as 2*v in VOLXL. If it does not, nothing else here means anything.
 * VOLXR is left in direct mode at a fixed value in every case as a second
 * control: it must not move while VOLXL sweeps.
 */

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/pcsxhw.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#include <stdint.h>

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)
#define SPU_ENDX_LOW (*(volatile uint16_t *)0x1f801d9c)
#define SPU_UPLOAD_ADDR 0x1080u

/* Packed 32-bit envelope, same field layout as the ADSR register pair. */
#define ATTACK(step, shift, exp) ((((step) & 3) << 8) | (((shift) & 31) << 10) | (!!(exp) << 15))
#define DECAY(shift) (((shift) & 15) << 4)
#define SUSTAIN(step, shift, level, direction, exp)                                       \
    ((((step) & 3) << 22) | (((shift) & 31) << 24) | (((level) & 15) << 0) |              \
     (!!(direction) << 30) | (!!(exp) << 31))
#define RELEASE(shift, exp) ((((shift) & 31) << 16) | (!!(exp) << 21))

/* Instant attack to full, no decay, sustain pinned at the top. Every case below
   starts from a known 0x7fff so the only variable is what happens after. */
#define HOLD_AT_PEAK (ATTACK(0, 0, 0) | DECAY(0) | SUSTAIN(3, 0, 15, 0, 0))


static const uint8_t kAdpcmSine[64] __attribute__((aligned(4))) = {
    0x00, 0x06, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x00, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x00, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x03, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
};


static uint8_t s_upload[128] __attribute__((aligned(4)));

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
    SPU_CTRL = (SPU_CTRL & ~0x0030);
    for (volatile int i = 0; i < 60; i++);
}

/* One tick, bounded on both halves. Returns 0 if SPUSTAT never moved, which
   would mean the SPU is not running and every tick count below is fiction. */
static int spu_tick(void) {
    int n = 0;
    while (!(SPU_STATUS & 0x0800)) {
        if (++n > 2000000) return 0;
    }
    while ((SPU_STATUS & 0x0800)) {
        if (++n > 2000000) return 0;
    }
    return 1;
}

static void upload_sample(int oneShot) {
    for (int i = 0; i < 64; i++) s_upload[i] = kAdpcmSine[i];
    for (int i = 64; i < 128; i++) s_upload[i] = 0xaa;
    /* Last block's flag byte: 0x03 is end+repeat (loops forever), 0x01 is end
       without repeat, which is the case psx-spx suspects mutes the envelope. */
    s_upload[3 * 16 + 1] = oneShot ? 0x01 : 0x03;
    spu_dma_write(SPU_UPLOAD_ADDR, s_upload, 128);
}


#define VOLXL(n) (*(volatile int16_t *)(0x1f801e00 + 4 * (n)))
#define VOLXR(n) (*(volatile int16_t *)(0x1f801e02 + 4 * (n)))
#define SWEEP(exp, dec, neg, shift, step) \
    (0x8000 | (!!(exp) << 14) | (!!(dec) << 13) | (!!(neg) << 12) | (((shift) & 31) << 2) | ((step) & 3))
#define TRACE_LEN 40
#define RIGHT_CTRL 0x1111

static int16_t s_trace[TRACE_LEN];

/* VOLXL trace per case, in run order, from an SCPH-1001. The same binary on
   an SCPH-5501 and an SCPH-7001 stays within 8 of it (one envelope step of
   sampling phase), so a sample passes within two steps. */
#define NUM_CASES 14
#define TOLERANCE 16
static const uint16_t kGolden[NUM_CASES][TRACE_LEN] = {
    {0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468, 0x2468},
    {0x06f9, 0x0df9, 0x1500, 0x1bf9, 0x22f9, 0x2a00, 0x30f9, 0x37f9, 0x3f00, 0x45f9, 0x4cf9, 0x53f9, 0x5af9, 0x61f9, 0x68f9, 0x6ff9, 0x7700, 0x7df9, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0x0200, 0x0400, 0x0600, 0x0800, 0x0a00, 0x0c00, 0x0e00, 0x1000, 0x1200, 0x1400, 0x1600, 0x1800, 0x1a00, 0x1c00, 0x1e00, 0x2000, 0x2200, 0x2400, 0x2600, 0x2800, 0x2a00, 0x2c00, 0x2e00, 0x3000, 0x3200, 0x3400, 0x3600, 0x3800, 0x3a00, 0x3c00, 0x3e00, 0x4000, 0x4200, 0x4400, 0x4600, 0x4800, 0x4a00, 0x4c00, 0x4e00, 0x5000},
    {0x0380, 0x0700, 0x0a80, 0x0e00, 0x1180, 0x1500, 0x1880, 0x1c00, 0x1f80, 0x2300, 0x2680, 0x2a00, 0x2d80, 0x3100, 0x3480, 0x3800, 0x3b80, 0x3f00, 0x4280, 0x4600, 0x4980, 0x4d00, 0x5080, 0x5400, 0x5780, 0x5b00, 0x5e80, 0x6200, 0x6580, 0x6900, 0x6c80, 0x7000, 0x7380, 0x7700, 0x7a80, 0x7e00, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0x77fe, 0x6ffe, 0x6806, 0x6006, 0x57fe, 0x5006, 0x4806, 0x3ffe, 0x3806, 0x3006, 0x27fe, 0x2006, 0x1806, 0x1006, 0x0806, 0x0006, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x06f9, 0x0df9, 0x1500, 0x1bf9, 0x22f9, 0x2a00, 0x30f9, 0x37f9, 0x3f00, 0x45f9, 0x4cf9, 0x5400, 0x5af9, 0x607f, 0x623f, 0x63ff, 0x65bf, 0x677f, 0x693f, 0x6aff, 0x6cbf, 0x6e7f, 0x703f, 0x71ff, 0x73bf, 0x757f, 0x773f, 0x78ff, 0x7abf, 0x7c7f, 0x7e3f, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0x77fe, 0x6ffe, 0x6905, 0x6205, 0x5bbb, 0x55bb, 0x4fc6, 0x4ac6, 0x45c6, 0x40c1, 0x3c9e, 0x389e, 0x349a, 0x309e, 0x2d76, 0x2a73, 0x2776, 0x2476, 0x2173, 0x1ef9, 0x1cf9, 0x1af7, 0x18f9, 0x16f9, 0x14f7, 0x12f9, 0x10f9, 0x0f7b, 0x0e7c, 0x0d7c, 0x0c7c, 0x0b7c, 0x0a7c, 0x097c, 0x087c, 0x077b, 0x067c, 0x057c, 0x047b, 0x037c},
    {0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000},
    {0xb808, 0xb008, 0xa800, 0xa008, 0x9808, 0x9000, 0x8808, 0x8008, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000},
    {0xc700, 0xcdf9, 0xd4f9, 0xdc00, 0xe2f9, 0xe9f9, 0xf0f9, 0xf7f9, 0xfef9, 0x05f9, 0x0cf9, 0x1400, 0x1af9, 0x21f9, 0x2900, 0x2ff9, 0x36f9, 0x3e00, 0x44f9, 0x4bf9, 0x5300, 0x59f9, 0x60f9, 0x6800, 0x6ef9, 0x75f9, 0x7d00, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0xc700, 0xcdf9, 0xd4f9, 0xdbf9, 0xe2f9, 0xe9f9, 0xf100, 0xf7f9, 0xfef9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0xc2fe, 0xc5fe, 0xc901, 0xcbfe, 0xcefe, 0xd155, 0xd355, 0xd555, 0xd755, 0xd955, 0xdb57, 0xdd55, 0xdf55, 0xe0ac, 0xe1ab, 0xe2ab, 0xe3ac, 0xe4ab, 0xe5ab, 0xe6ac, 0xe7ab, 0xe8ab, 0xe9ac, 0xeaab, 0xebab, 0xecac, 0xedab, 0xeeab, 0xefac, 0xf001, 0xf001, 0xf001, 0xf001, 0xf001, 0xf001, 0xf001, 0xf001, 0xf001, 0xf001, 0xf001},
    {0x1800, 0x1000, 0x0808, 0x0008, 0xf808, 0xf008, 0xe808, 0xe008, 0xd808, 0xd000, 0xc808, 0xc008, 0xb800, 0xb008, 0xa808, 0xa000, 0x9808, 0x9008, 0x8800, 0x8008, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000}
};
static int s_case = 0;
static int s_failures = 0;

static void run_case(const char *tag, uint16_t startDirect, uint16_t sweep, int stride) {
    SPU_VOICES[1].volumeLeft = startDirect;
    SPU_VOICES[1].volumeRight = RIGHT_CTRL;
    int stalled = 0;
    for (int i = 0; i < 3; i++) stalled += !spu_tick();
    int16_t before = VOLXL(1);
    SPU_VOICES[1].volumeLeft = sweep;
    for (int i = 0; i < TRACE_LEN; i++) {
        for (int s = 0; s < stride; s++) stalled += !spu_tick();
        s_trace[i] = VOLXL(1);
    }
    ramsyscall_printf("OBS volsweep %s start=%04x sweep=%04x stride=%d before=%04x", tag, startDirect, sweep,
                      stride, (uint16_t)before);
    for (int i = 0; i < TRACE_LEN; i++) ramsyscall_printf(" %04x", (uint16_t)s_trace[i]);
    ramsyscall_printf(" volxr=%04x\n", (uint16_t)VOLXR(1));

    int bad = -1;
    for (int i = 0; i < TRACE_LEN && bad < 0; i++) {
        int d = (int)s_trace[i] - (int16_t)kGolden[s_case][i];
        if (d < -TOLERANCE || d > TOLERANCE) bad = i;
    }
    if ((uint16_t)VOLXR(1) != (uint16_t)(RIGHT_CTRL * 2)) {
        ramsyscall_printf("SPUVOLSWEEP: FAIL %s - VOLXR control moved to %04x\n", tag, (uint16_t)VOLXR(1));
        s_failures++;
    }
    if (stalled) {
        ramsyscall_printf("SPUVOLSWEEP: FAIL %s - SPUSTAT bit 11 stopped moving %d times\n", tag, stalled);
        s_failures++;
    }
    if (bad >= 0) {
        ramsyscall_printf("SPUVOLSWEEP: FAIL %s - sample %d reads %04x, expected %04x\n", tag, bad,
                          (uint16_t)s_trace[bad], kGolden[s_case][bad]);
        s_failures++;
    }
    s_case++;
}

int main() {
    ramsyscall_printf("OBS volsweep start trace=%d\n", TRACE_LEN);
    SPU_CTRL = 0;
    for (volatile int i = 0; i < 60; i++);
    SPU_RAM_DTC = 4;
    for (volatile int i = 0; i < 60; i++);
    DPCR |= 0x000b0000;
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_REVERB_LEFT = 0;
    SPU_REVERB_RIGHT = 0;
    SPU_PITCH_MOD_LOW = 0;
    SPU_PITCH_MOD_HIGH = 0;
    SPU_NOISE_EN_LOW = 0;
    SPU_NOISE_EN_HIGH = 0;
    SPU_REVERB_EN_LOW = 0;
    SPU_REVERB_EN_HIGH = 0;
    SPU_REVERB_ADDR = 0xffff;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_CTRL = 0x8000;
    upload_sample(0);
    SPU_CTRL = 0x8000 | 0x4000;
    SPU_VOICES[1].sampleStartAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[1].sampleRepeatAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[1].sampleRate = 0x1000;
    SPU_VOICES[1].volumeLeft = 0;
    SPU_VOICES[1].volumeRight = 0;
    SPU_VOICES[1].adsrLo = (uint16_t)(HOLD_AT_PEAK & 0xffff);
    SPU_VOICES[1].adsrHi = (uint16_t)(HOLD_AT_PEAK >> 16);
    SPU_KEY_OFF_LOW = 0;
    SPU_KEY_OFF_HIGH = 0;
    SPU_KEY_ON_LOW = 1u << 1;
    SPU_KEY_ON_HIGH = 0;
    if (!spu_tick()) {
        ramsyscall_printf("OBS volsweep FATAL spustat bit 11 never moved\n");
        pcsx_exit(1);
        return 1;
    }

    /* instrument control: direct 0x1234 must read 0x2468 */
    run_case("direct", 0x1234, 0x1234, 1);
    /* linear increase from 0, shift 12/13/14, step 0 (+7) and 3 (+4) */
    run_case("lin_inc_s12_t0", 0, SWEEP(0, 0, 0, 12, 0), 1);
    run_case("lin_inc_s13_t3", 0, SWEEP(0, 0, 0, 13, 3), 1);
    run_case("lin_inc_s16_t0", 0, SWEEP(0, 0, 0, 16, 0), 8);
    /* linear decrease from 0x3fff (VOLX 0x7ffe) */
    run_case("lin_dec_s12_t0", 0x3fff, SWEEP(0, 1, 0, 12, 0), 1);
    /* exponential */
    run_case("exp_inc_s12_t0", 0, SWEEP(1, 0, 0, 12, 0), 1);
    run_case("exp_dec_s12_t0", 0x3fff, SWEEP(1, 1, 0, 12, 0), 1);
    /* fast: does shift 4 saturate within a tick */
    run_case("lin_inc_s04_t0", 0, SWEEP(0, 0, 0, 4, 0), 1);
    /* negative phase from a negative start: direct 0x6000 = VOLX -0x4000 */
    run_case("neg_start_ctrl", 0x6000, 0x6000, 1);
    run_case("lin_inc_s12_ph1_neg", 0x6000, SWEEP(0, 0, 1, 12, 0), 1);
    run_case("lin_inc_s12_ph0_neg", 0x6000, SWEEP(0, 0, 0, 12, 0), 1);
    run_case("lin_dec_s12_ph1_neg", 0x6000, SWEEP(0, 1, 1, 12, 0), 1);
    run_case("exp_dec_s12_ph1_neg", 0x6000, SWEEP(1, 1, 1, 12, 0), 1);
    /* phase bit with a positive start */
    run_case("lin_inc_s12_ph1_pos", 0x1000, SWEEP(0, 0, 1, 12, 0), 1);

    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    if (s_case != NUM_CASES) {
        ramsyscall_printf("SPUVOLSWEEP: FAIL - ran %d cases, the golden table has %d\n", s_case, NUM_CASES);
        s_failures++;
    }
    ramsyscall_printf("SPUVOLSWEEP: %s (%d failures)\n", s_failures ? "FAILURE" : "PASS", s_failures);
    pcsx_exit(s_failures ? 1 : 0);
    return s_failures ? 1 : 0;
}
