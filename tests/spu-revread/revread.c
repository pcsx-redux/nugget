/*
 * Does the reverb unit read its work area and drive the output when SPUCNT
 * bit 7 (reverb master enable) is clear?
 *
 * The CPU cannot see the answer: the SPU RAM capture buffers are taken before
 * the reverb, so this is graded on the digital audio stream (final mix) with a
 * logic analyzer, like spu-revstream. Nothing here asserts on the console.
 *
 * No voice is keyed. A known pattern is written into the work area by DMA and
 * the reverb registers collapse the network so the output is a plain readout
 * of one work-area tap per side:
 *
 *   vCOMB1..4 = 0, vAPF1 = vAPF2 = 0  ->  Lout = [mLAPF2 - dAPF2] * vLOUT
 *   vIIR = 7fff, vWALL = 0, vLIN = 0  ->  every work-area WRITE stores ~0
 *
 * The read pointer walks the whole work area once per pass, so a unit that
 * reads plays the pattern back with a period of SIZE/2 samples at 22.05kHz.
 * A unit that also writes zeroes the work area within one pass, which is what
 * separates "reads only" from "reads and writes":
 *
 *   ARM1A  bit 7 clear, reverb out volume on    -> the question
 *   ARM3   bit 7 clear, reverb out volume 0     -> negative control
 *   ARM1B  bit 7 clear, reverb out volume on    -> still the same pattern
 *                                                  7s later = nothing wrote it
 *   ARM2   bit 7 set 1000ms after the marker    -> positive control: pattern
 *                                                  until the writes catch the
 *                                                  read tap, then silence
 *
 * Each marker is printed after the state it names is in place, except ARM2,
 * whose transition is meant to land inside the capture window.
 */

#include "common/hardware/counters.h"
#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#include <stdint.h>

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)

/* 8KB work area: 4096 samples, one pass every 4096 / 22050 = 185.8ms. */
#define WORK_SIZE 0x2000u
#define WORK_BASE (0x80000u - WORK_SIZE)

#define CTRL_ON 0xc000u
#define CTRL_REVERB 0x0080u

/* Distinct levels per side so a channel swap in the decode cannot hide. */
#define OUT_LEFT 0x4000u
#define OUT_RIGHT 0x2000u

/* Read taps: mLAPF2 - dAPF2 = 0x0f0, mRAPF2 - dAPF2 = 0x0e8 (8-byte units).
   Every write target except APF2's sits below the taps, so once bit 7 is set
   a tapped cell was last written almost a whole pass earlier; APF2 writes
   dAPF2 = 0x300 units (3072 samples, 139ms) ahead of its tap. That makes the
   ARM2 transition a pattern burst of roughly 139ms before the zeroes arrive,
   long enough to see in one capture. */
static const uint16_t kReadoutPreset[32] = {
    // dAPF1  dAPF2  vIIR   vCOMB1 vCOMB2 vCOMB3 vCOMB4 vWALL
    0x0080, 0x0300, 0x7fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    // vAPF1  vAPF2  mLSAME mRSAME mLCOMB1 mRCOMB1 mLCOMB2 mRCOMB2
    0x0000, 0x0000, 0x0040, 0x0038, 0x0080, 0x0078, 0x0070, 0x0068,
    // dLSAME dRSAME mLDIFF mRDIFF mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4
    0x0030, 0x0028, 0x0020, 0x0018, 0x0060, 0x0058, 0x0050, 0x0048,
    // dLDIFF dRDIFF mLAPF1 mRAPF1 mLAPF2 mRAPF2 vLIN   vRIN
    0x0010, 0x0008, 0x00a0, 0x0098, 0x03f0, 0x03e8, 0x0000, 0x0000,
};

static uint16_t s_pattern[WORK_SIZE / 2] __attribute__((aligned(4)));

/* Full-scale LFSR noise at +-0x2000: aperiodic inside one pass, so the only
   repeat in the output is the pass itself, and cross-correlation against an
   emulator capture has one peak per pass. */
static void pattern_build(void) {
    uint16_t lfsr = 0xACE1u;
    for (unsigned i = 0; i < WORK_SIZE / 2; i++) {
        lfsr = (uint16_t)((lfsr >> 1) ^ (-(int16_t)(lfsr & 1u) & 0xB400u));
        s_pattern[i] = (lfsr & 1u) ? 0x2000u : (uint16_t)-0x2000;
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
    while ((DMA_CTRL[DMA_SPU].CHCR & 0x01000000) != 0) __asm__ volatile("");
    SPU_CTRL = (SPU_CTRL & ~0x0030);
    for (volatile int i = 0; i < 60; i++);
}

/* Root counter 2 at sysclk/8, 4.2336MHz, free-running. */
static void hold_ms(unsigned ms) {
    const uint32_t target = ms * 4234u;
    uint32_t elapsed = 0;
    uint16_t prev = COUNTERS[2].value;
    while (elapsed < target) {
        uint16_t now = COUNTERS[2].value;
        elapsed += (uint16_t)(now - prev);
        prev = now;
    }
}

static void reverb_out(uint16_t left, uint16_t right) {
    SPU_REVERB_LEFT = left;
    SPU_REVERB_RIGHT = right;
}

int main(void) {
    ramsyscall_printf("REVREAD: start\n");
    COUNTERS[2].mode = TM_CLK_DIV8;

    DPCR |= 0x000b0000;
    SPU_CTRL = 0;
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    reverb_out(0, 0);
    SPU_VOL_CD_LEFT = 0;
    SPU_VOL_CD_RIGHT = 0;
    SPU_VOL_EXT_LEFT = 0;
    SPU_VOL_EXT_RIGHT = 0;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_PITCH_MOD_LOW = 0;
    SPU_PITCH_MOD_HIGH = 0;
    SPU_NOISE_EN_LOW = 0;
    SPU_NOISE_EN_HIGH = 0;
    SPU_REVERB_EN_LOW = 0;
    SPU_REVERB_EN_HIGH = 0;
    SPU_RAM_DTC = 4;
    /* An upload with the SPU disabled is dropped on hardware. Bit 7 stays
       clear from here to ARM2, so nothing the unit does can write the work
       area behind the DMA. */
    SPU_CTRL = CTRL_ON;

    pattern_build();
    for (unsigned off = 0; off < WORK_SIZE; off += 0x800) {
        spu_dma_write(WORK_BASE + off, (const uint8_t *)s_pattern + off, 0x800);
    }

    SPU_REVERB_ADDR = WORK_BASE >> 3;
    {
        volatile uint16_t *regs = (volatile uint16_t *)SPU_REVERB;
        for (unsigned i = 0; i < 32; i++) regs[i] = kReadoutPreset[i];
    }
    SPU_VOL_MAIN_LEFT = 0x3fff;
    SPU_VOL_MAIN_RIGHT = 0x3fff;

    reverb_out(OUT_LEFT, OUT_RIGHT);
    ramsyscall_printf("REVREAD-ARM1A bit7=0 out=on\n");
    hold_ms(4000);

    reverb_out(0, 0);
    ramsyscall_printf("REVREAD-ARM3 bit7=0 out=off\n");
    hold_ms(3000);

    reverb_out(OUT_LEFT, OUT_RIGHT);
    ramsyscall_printf("REVREAD-ARM1B bit7=0 out=on\n");
    hold_ms(3000);

    ramsyscall_printf("REVREAD-ARM2 bit7=1 in 1000ms\n");
    hold_ms(1000);
    SPU_CTRL = CTRL_ON | CTRL_REVERB;
    hold_ms(3000);

    ramsyscall_printf("REVREAD: done\n");
    while (1) __asm__ volatile("");
    return 0;
}
