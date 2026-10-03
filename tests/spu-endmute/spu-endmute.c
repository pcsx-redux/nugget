/*
 * ENVX (1F801C0Ch+N*10h) after an ADPCM end block without repeat.
 *
 * psx-spx, flag byte code 1 ("End+Mute"): jump to the repeat address, set ENDX,
 * Release, Env=0000h. So a one-shot voice that is never keyed off still reads
 * ENVX = 0 once its last block has played, and games poll for exactly that to
 * notice a sound has finished.
 *
 * The time it goes to zero depends on the pitch, so it is checked against the
 * SPUSTAT bit 11 clock, which toggles every 256 output samples:
 *
 *   A: 16 blocks (448 ADPCM samples) at pitch 0400h last 1792 output samples.
 *      ENVX must still be at its peak a few toggles in, and read 0 around
 *      toggle 7-8.
 *   B: same sample at pitch 0200h, switched to 1000h at toggle 4 with no ENVX
 *      read in between. The end lands about 1100-1350 samples after key-on, so
 *      the first zero is the read after toggle 6. Applying 1000h from key-on
 *      would end it before toggle 4, and ignoring the change near toggle 14.
 *
 * Voice 3 plays the same blocks looping (loop start on the first block, end and
 * repeat on the last) and must hold its envelope throughout, so a zero on voice
 * 1 is the end block and not something silencing every voice.
 */

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/pcsxhw.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#include <stdint.h>

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)
#define ONESHOT_ADDR 0x1080u
#define LOOP_ADDR 0x1180u
#define BLOCKS 16
#define READS 20

static const uint8_t kSineBody[14] = {0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13,
                                      0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec};

static uint8_t s_upload[2 * BLOCKS * 16] __attribute__((aligned(4)));

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

// Wait for the next edge of SPUSTAT bit 11, in either direction: 256 output samples.
// Returns 0 if it never moves, in which case no count below means anything.
static int spu_toggle(void) {
    const uint16_t start = SPU_STATUS & 0x0800;
    for (int n = 0; n < 2000000; n++) {
        if ((SPU_STATUS & 0x0800) != start) return 1;
    }
    return 0;
}

static void voice_setup(int v, uint32_t addr, uint16_t pitch) {
    SPU_VOICES[v].sampleStartAddr = addr >> 3;
    SPU_VOICES[v].sampleRepeatAddr = addr >> 3;
    SPU_VOICES[v].sampleRate = pitch;
    SPU_VOICES[v].adsrLo = 0x000f;
    SPU_VOICES[v].adsrHi = 0x80ff;  // release shift 31: release alone never reaches 0 here
    SPU_VOICES[v].volumeLeft = 0;
    SPU_VOICES[v].volumeRight = 0;
}

static uint16_t s_v1[READS];
static uint16_t s_v3[READS];

// Key on, then read ENVX right after each of the next READS toggles. Read 0 is taken
// after the first toggle. When switchAt is non-zero, the voice 1 pitch is rewritten to
// newPitch right after toggle switchAt, and the reads before it are skipped.
static int run_case(const char *name, uint16_t pitch, int switchAt, uint16_t newPitch, int lo, int hi) {
    voice_setup(1, ONESHOT_ADDR, pitch);
    voice_setup(3, LOOP_ADDR, pitch);
    SPU_KEY_ON_LOW = (1u << 1) | (1u << 3);
    SPU_KEY_ON_HIGH = 0;

    for (int i = 0; i < READS; i++) {
        if (!spu_toggle()) {
            ramsyscall_printf("SPUENDMUTE: %s FAIL - SPUSTAT bit 11 stopped moving\n", name);
            return 1;
        }
        if (switchAt && i < switchAt) {
            s_v1[i] = 0xffff;
            s_v3[i] = 0xffff;
            if (i == switchAt - 1) SPU_VOICES[1].sampleRate = newPitch;
            continue;
        }
        s_v1[i] = SPU_VOICES[1].currentVolume;
        s_v3[i] = SPU_VOICES[3].currentVolume;
    }

    int failures = 0;
    int firstZero = -1;
    int peakSeen = 0;
    for (int i = 0; i < READS; i++) {
        ramsyscall_printf("SPUENDMUTE: %s toggle %02d envx1=%04x envx3=%04x\n", name, i + 1, s_v1[i], s_v3[i]);
        if (s_v1[i] == 0xffff) continue;
        if (s_v1[i] == 0x7fff && firstZero < 0) peakSeen = 1;
        if (s_v1[i] == 0 && firstZero < 0) firstZero = i + 1;
        if (firstZero >= 0 && s_v1[i] != 0) {
            ramsyscall_printf("SPUENDMUTE: %s FAIL - voice 1 left zero again at toggle %d\n", name, i + 1);
            failures++;
            break;
        }
    }
    for (int i = 0; i < READS; i++) {
        if (s_v3[i] != 0xffff && s_v3[i] != 0x7fff) {
            ramsyscall_printf("SPUENDMUTE: %s FAIL - looping control left its peak at toggle %d\n", name, i + 1);
            failures++;
            break;
        }
    }
    if (!peakSeen) {
        ramsyscall_printf("SPUENDMUTE: %s FAIL - voice 1 never read its peak before ending\n", name);
        failures++;
    }
    if (firstZero < lo || firstZero > hi) {
        ramsyscall_printf("SPUENDMUTE: %s FAIL - voice 1 first read 0 at toggle %d, expected %d..%d\n", name,
                          firstZero, lo, hi);
        failures++;
    }
    ramsyscall_printf("SPUENDMUTE: %s firstZero=%d %s\n", name, firstZero, failures ? "FAIL" : "ok");
    return failures;
}

int main() {
    ramsyscall_printf("SPUENDMUTE: start\n");

    DPCR |= 0x000b0000;
    SPU_CTRL = 0;
    for (volatile int i = 0; i < 60; i++);
    SPU_RAM_DTC = 4;
    for (volatile int i = 0; i < 60; i++);
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    for (volatile int i = 0; i < 60; i++);

    for (int b = 0; b < 2 * BLOCKS; b++) {
        uint8_t *block = s_upload + b * 16;
        block[0] = 0x00;
        block[1] = 0x00;
        for (int i = 0; i < 14; i++) block[2 + i] = kSineBody[i];
    }
    s_upload[(BLOCKS - 1) * 16 + 1] = 0x01;      // one-shot: end, no repeat
    s_upload[BLOCKS * 16 + 1] = 0x04;            // looping control: loop start
    s_upload[(2 * BLOCKS - 1) * 16 + 1] = 0x03;  // looping control: end and repeat
    // The SPU has to be enabled for the transfer to land on hardware.
    SPU_CTRL = 0x8000;
    for (volatile int i = 0; i < 60; i++);
    spu_dma_write(ONESHOT_ADDR, s_upload, sizeof(s_upload));

    SPU_CTRL = 0x8000 | 0x4000;
    for (volatile int i = 0; i < 60; i++);

    int failures = 0;
    failures += run_case("A", 0x0400, 0, 0, 6, 9);
    failures += run_case("B", 0x0200, 4, 0x1000, 6, 7);

    ramsyscall_printf("SPUENDMUTE: %s\n", failures ? "FAILURE" : "PASS");
    pcsx_exit(failures ? 1 : 0);
    return failures ? 1 : 0;
}
