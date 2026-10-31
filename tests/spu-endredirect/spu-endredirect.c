/*
 * Where a voice goes after an ADPCM end block, with and without the repeat flag.
 *
 * psx-spx, flag byte bits 0-1: code 3 (End+Repeat) and code 1 (End+Mute) both
 * "jump to Loop-address" and set ENDX; code 1 additionally forces Release with
 * Env=0000h. The voice is silenced, not stopped, and "all voices are permanently
 * reading data from SPU RAM" (Voice Interrupt), so after a code 1 block the
 * readout carries on from the repeat address and can still raise IRQ9 there.
 * psx-spx names the use: redirecting a one-shot sample "to a silent-loop located
 * elsewhere in memory" by writing the repeat address.
 *
 * The one-shot sample has no loop-start flag anywhere, so its repeat address is
 * only ever what the CPU writes, once the voice is playing. The silent loop it is
 * redirected to sits BELOW the one-shot, so a cursor that ignored the end flag and
 * walked forward could not reach it without wrapping all of SPU RAM. IRQA sits on
 * a block boundary two blocks into the loop, never on the loop address itself.
 *
 * Case A, end block code 3, is the control: it proves the redirect, the upload and
 * the IRQ flag. Case B, end block code 1, is the subject. Every other voice gets
 * pitch 0, so no idle voice can walk into IRQA on its own.
 */

#include <stdint.h>

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/pcsxhw.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)
#define SPU_ENDX_LOW (*(volatile uint16_t *)0x1f801d9c)

#define LOOP_ADDR 0x1000u
#define LOOP_BLOCKS 4
#define ONESHOT_ADDR 0x2000u
#define ONESHOT_BLOCKS 16
#define IRQ_BYTE_ADDR (LOOP_ADDR + 2 * 16)
#define PITCH 0x0400

#define SPU_CTRL_ENABLE 0x8000u
#define SPU_CTRL_UNMUTE 0x4000u
#define SPU_CTRL_IRQ_ENABLE 0x0040u
#define SPU_STATUS_IRQ_FLAG 0x0040u

/* The one-shot lasts about 40 ms at this pitch and the loop lap about 10 ms, so a
   live readout matches well inside these budgets and only a dead one spends them. */
#define IRQ_POLL_LIMIT 4000000u
#define ENVX_POLL_LIMIT 4000000u

static uint8_t s_loop[LOOP_BLOCKS * 16] __attribute__((aligned(4)));
static uint8_t s_oneshot[ONESHOT_BLOCKS * 16] __attribute__((aligned(4)));

static void make_loop(void) {
    for (int block = 0; block < LOOP_BLOCKS; block++) {
        uint8_t *p = &s_loop[block * 16];
        for (int i = 0; i < 16; i++) p[i] = 0;
        if (block == 0) p[1] = 0x04;               /* loop start */
        if (block == LOOP_BLOCKS - 1) p[1] = 0x03; /* end + repeat */
    }
}

static void make_oneshot(uint8_t endFlags) {
    for (int block = 0; block < ONESHOT_BLOCKS; block++) {
        uint8_t *p = &s_oneshot[block * 16];
        p[0] = 0x00;
        p[1] = block == ONESHOT_BLOCKS - 1 ? endFlags : 0x00;
        for (int i = 2; i < 16; i++) p[i] = 0x37;
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
    /* The SPU is still writing the last ~60 bytes when CHCR clears, and SPUSTAT
       bit 10 never reads busy by then: wait a fixed time before leaving transfer
       mode, or both samples lose their end blocks on hardware. */
    for (volatile int i = 0; i < 20000; i++);
    SPU_CTRL = (SPU_CTRL & ~0x0030);
    for (volatile int i = 0; i < 60; i++);
}

static int upload(uint32_t addr, const void *src, uint32_t bytes) {
    spu_dma_write(addr, src, bytes);
    if (DMA_CTRL[DMA_SPU].CHCR & 0x01000000) {
        ramsyscall_printf("SPUENDREDIRECT: FAIL - the upload to %04x never finished\n", (unsigned)addr);
        return 0;
    }
    return 1;
}

static void irq_disable(void) {
    SPU_CTRL = SPU_CTRL & ~SPU_CTRL_IRQ_ENABLE;
    for (uint32_t i = 0; i < 100000 && (SPU_STATUS & SPU_STATUS_IRQ_FLAG); i++);
}

static void irq_arm(uint32_t byteAddr) {
    SPU_IRQ_ADDR = (uint16_t)(byteAddr >> 3);
    SPU_CTRL = (SPU_CTRL & ~0x0030) | SPU_CTRL_ENABLE | SPU_CTRL_UNMUTE | SPU_CTRL_IRQ_ENABLE;
    for (volatile int i = 0; i < 120; i++);
}

static uint32_t irq_poll(uint32_t limit) {
    for (uint32_t i = 0; i < limit; i++) {
        if (SPU_STATUS & SPU_STATUS_IRQ_FLAG) return i + 1;
        __asm__ volatile("");
    }
    return 0;
}

/* Returns the number of failed checks. */
static int run_case(const char *name, uint8_t endFlags, int expectMute) {
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    irq_disable();
    for (volatile int i = 0; i < 200000; i++);

    make_oneshot(endFlags);
    if (!upload(ONESHOT_ADDR, s_oneshot, sizeof(s_oneshot))) return 1;

    SPU_VOICES[1].sampleStartAddr = ONESHOT_ADDR >> 3;
    SPU_VOICES[1].sampleRate = PITCH;
    SPU_VOICES[1].adsrLo = 0x000f;
    SPU_VOICES[1].adsrHi = 0x80ff; /* release alone never reaches 0 inside this test */
    SPU_VOICES[1].volumeLeft = 0;
    SPU_VOICES[1].volumeRight = 0;

    irq_arm(IRQ_BYTE_ADDR);
    SPU_KEY_ON_LOW = 1u << 1;
    SPU_KEY_ON_HIGH = 0;

    /* Redirect only once the voice is playing, so whatever KEY ON does to the
       repeat address has already happened. */
    uint32_t envxPolls = 0;
    while (envxPolls < ENVX_POLL_LIMIT && SPU_VOICES[1].currentVolume == 0) envxPolls++;
    const uint16_t envxPlaying = SPU_VOICES[1].currentVolume;
    SPU_VOICES[1].sampleRepeatAddr = LOOP_ADDR >> 3;

    const uint32_t polls = irq_poll(IRQ_POLL_LIMIT);
    for (volatile int i = 0; i < 200000; i++);
    const uint16_t envxAfter = SPU_VOICES[1].currentVolume;
    const uint16_t endx = SPU_ENDX_LOW;
    const uint16_t repeat = SPU_VOICES[1].sampleRepeatAddr;
    irq_disable();

    ramsyscall_printf(
        "SPUENDREDIRECT: %s end=%02x fired=%d polls=%lu envxPlaying=%04x envxAfter=%04x "
        "endx=%04x repeat=%04x\n",
        name, endFlags, polls != 0, (unsigned long)polls, envxPlaying, envxAfter, endx, repeat);

    int failures = 0;
    if (envxPlaying == 0) {
        ramsyscall_printf("SPUENDREDIRECT: %s FAIL - voice 1 never started, nothing below means anything\n", name);
        return 1;
    }
    if (!(endx & 2)) {
        ramsyscall_printf(
            "SPUENDREDIRECT: %s FAIL - ENDX never latched, the voice did not reach its end "
            "block\n",
            name);
        failures++;
    }
    if (expectMute && envxAfter != 0) {
        ramsyscall_printf(
            "SPUENDREDIRECT: %s FAIL - ENVX=%04x after an end block without repeat, expected "
            "0\n",
            name, envxAfter);
        failures++;
    }
    if (!expectMute && envxAfter == 0) {
        ramsyscall_printf("SPUENDREDIRECT: %s FAIL - ENVX fell to 0 after an end block with repeat\n", name);
        failures++;
    }
    if (polls == 0) {
        ramsyscall_printf(
            "SPUENDREDIRECT: %s FAIL - no IRQ at %04x, inside the loop the repeat address "
            "points to, in %lu polls\n",
            name, (unsigned)IRQ_BYTE_ADDR, (unsigned long)IRQ_POLL_LIMIT);
        failures++;
    }
    return failures;
}

int main() {
    ramsyscall_printf("SPUENDREDIRECT: start\n");

    DPCR |= 0x000b0000; /* DMA4 (SPU) on, or the uploads below are silent no-ops */
    SPU_CTRL = 0;
    for (volatile int i = 0; i < 60; i++);
    SPU_RAM_DTC = 4; /* required on real hardware for SPU RAM transfers */
    for (volatile int i = 0; i < 60; i++);
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_IRQ_ADDR = 0;
    for (int v = 0; v < 24; v++) SPU_VOICES[v].sampleRate = 0;
    for (volatile int i = 0; i < 60; i++);

    SPU_CTRL = SPU_CTRL_ENABLE;
    for (volatile int i = 0; i < 60; i++);

    make_loop();
    if (!upload(LOOP_ADDR, s_loop, sizeof(s_loop))) {
        pcsx_exit(1);
        return 1;
    }

    SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_UNMUTE;
    for (volatile int i = 0; i < 60; i++);

    const int controlFailures = run_case("A", 0x03, 0);
    const int failures = run_case("B", 0x01, 1);

    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_CTRL = 0;

    if (controlFailures) {
        ramsyscall_printf("SPUENDREDIRECT: FAILURE - the end+repeat control failed, case B proves nothing\n");
    }
    ramsyscall_printf("SPUENDREDIRECT: %s (control=%d subject=%d)\n", controlFailures + failures ? "FAILURE" : "PASS",
                      controlFailures, failures);
    pcsx_exit(controlFailures + failures ? 1 : 0);
    return controlFailures + failures ? 1 : 0;
}
