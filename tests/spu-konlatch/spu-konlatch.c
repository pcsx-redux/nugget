/*
 * Key-on timeline and the repeat address (LSAX) write latch, pinned from silicon.
 *
 * Measured on SCPH-1001, SCPH-5501 and SCPH-7001 with identical results: KEY ON is
 * serviced on every other sample, ENDX clears when it is, the voice then fetches its
 * first block's header on three consecutive samples and applies that block's
 * loop-start flag each time, and the envelope takes its first step four samples after
 * ENDX cleared. A CPU write to the repeat address only stops later loop-start flags from
 * replacing it once the third of those fetches has passed.
 *
 * Voice 1 plays SAMPLE: block 0 and block 8 carry loop-start, block 15 is end+repeat.
 * MARKER is a separate end+repeat block with no loop-start, so following it cannot
 * re-latch anything. Every check reads timer 2 at CPU/8, 96 ticks per output sample.
 */
#include "common/hardware/counters.h"
#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#include <stdint.h>

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)
#define SPU_RAM_CTRL (*(volatile uint16_t *)0xbf801dac)
#define ENDX_LOW (*(volatile uint16_t *)0xbf801d9c)
#define VOICE 1
#define VOICE_BIT (1u << VOICE)
#define SAMPLE_ADDR 0x2000u
#define MARKER_ADDR 0x3000u
#define SAMPLE_LSAX (SAMPLE_ADDR >> 3)
#define BLOCK8_LSAX ((SAMPLE_ADDR + 8 * 16) >> 3)
#define MARKER_LSAX (MARKER_ADDR >> 3)
#define IMAGE_SIZE (MARKER_ADDR + 64 - SAMPLE_ADDR)
#define TICKS 96u
#define POLL_LIMIT 2000000u
#define NONE 0xffffu

static uint8_t s_image[IMAGE_SIZE] __attribute__((aligned(4)));
static uint8_t s_readback[IMAGE_SIZE] __attribute__((aligned(4)));
static int s_failures;

static uint16_t now(void) { return (uint16_t)COUNTERS[2].value; }

static int wait_ticks(unsigned ticks) {
    while (ticks) {
        const unsigned n = ticks > 0x4000 ? 0x4000 : ticks;
        const uint16_t origin = now();
        unsigned i = 0;
        while ((uint16_t)(now() - origin) < n)
            if (++i > POLL_LIMIT) return 0;
        ticks -= n;
    }
    return 1;
}

static int wait_edge(uint16_t *at) {
    unsigned n = POLL_LIMIT;
    while (!(SPU_STATUS & 0x0800))
        if (!--n) return 0;
    n = POLL_LIMIT;
    while (SPU_STATUS & 0x0800)
        if (!--n) return 0;
    *at = now();
    return 1;
}

static void check(int ok, const char *what) {
    if (ok) return;
    s_failures++;
    ramsyscall_printf("SPUKONLATCH: FAIL %s\n", what);
}

static int dma(void *data, int read) {
    const uint16_t mode = read ? 0x30 : 0x20;
    SPU_CTRL = 0xc000;
    if (!wait_ticks(4 * TICKS)) return 0;
    SPU_RAM_DTA = SAMPLE_ADDR >> 3;
    if (!wait_ticks(TICKS)) return 0;
    SPU_CTRL = 0xc000 | mode;
    unsigned n = POLL_LIMIT;
    while ((SPU_STATUS & 0x30) != mode)
        if (!--n) return 0;
    SPU_DELAY = (SPU_DELAY & 0xf0ffffff) | (read ? 0x22000000 : 0x20000000);
    DMA_CTRL[DMA_SPU].MADR = (uint32_t)data & 0x1fffffff;
    DMA_CTRL[DMA_SPU].BCR = ((IMAGE_SIZE / 64) << 16) | 0x10;
    DMA_CTRL[DMA_SPU].CHCR = read ? 0x01000200 : 0x01000201;
    n = POLL_LIMIT;
    while (DMA_CTRL[DMA_SPU].CHCR & 0x01000000)
        if (!--n) return 0;
    // The tail of a write is still draining into SPU RAM when CHCR clears.
    if (!wait_ticks(128 * TICKS)) return 0;
    SPU_CTRL = 0xc000;
    return wait_ticks(4 * TICKS);
}

static int upload(void) {
    for (unsigned i = 0; i < IMAGE_SIZE; i++) s_image[i] = 0;
    for (unsigned block = 0; block < 16; block++) {
        uint8_t *p = s_image + block * 16;
        p[0] = 0x00;
        p[1] = (block == 0 || block == 8) ? 0x04 : block == 15 ? 0x03 : 0x00;
        for (unsigned i = 2; i < 16; i++) p[i] = 0x11;
    }
    uint8_t *marker = s_image + (MARKER_ADDR - SAMPLE_ADDR);
    marker[1] = 0x03;
    for (unsigned i = 2; i < 16; i++) marker[i] = 0x11;
    if (!dma(s_image, 0) || !dma(s_readback, 1)) return 0;
    for (unsigned i = 0; i < IMAGE_SIZE; i++)
        if (s_readback[i] != s_image[i]) return 0;
    return 1;
}

typedef struct {
    uint16_t edge, kon, endxClear, attack, finalLsax;
    unsigned fetches;
} Trial;

enum { WRITE_NONE, WRITE_AFTER_KON, WRITE_AFTER_ATTACK, WRITE_FETCHES };

/* Key voice 1 on `phase` samples after a bit-11 falling edge and trace it for 16
 * samples. ENDX is set beforehand by letting the voice reach its end block. */
static int trial(Trial *t, int phase, int mode) {
    SPU_KEY_OFF_LOW = VOICE_BIT;
    SPU_VOICES[VOICE].adsrHi = 0x0000;
    if (!wait_ticks(32 * TICKS)) return 0;
    if (SPU_VOICES[VOICE].currentVolume != 0) return 0;
    SPU_KEY_OFF_LOW = 0;
    SPU_VOICES[VOICE].sampleRate = 0x4000;
    SPU_VOICES[VOICE].sampleStartAddr = SAMPLE_ADDR >> 3;
    SPU_VOICES[VOICE].adsrLo = 0x000f;
    SPU_VOICES[VOICE].adsrHi = 0x1fc0;
    // Run the sample to its end once so ENDX is set, then key it off again.
    SPU_KEY_ON_LOW = VOICE_BIT;
    if (!wait_ticks(160 * TICKS)) return 0;
    SPU_KEY_OFF_LOW = VOICE_BIT;
    SPU_VOICES[VOICE].adsrHi = 0x0000;
    if (!wait_ticks(32 * TICKS)) return 0;
    if (!(ENDX_LOW & VOICE_BIT) || SPU_VOICES[VOICE].currentVolume != 0) return 0;
    SPU_KEY_OFF_LOW = 0;
    SPU_VOICES[VOICE].sampleRate = 0x1000;
    SPU_VOICES[VOICE].adsrHi = 0x1fc0;
    SPU_VOICES[VOICE].sampleRepeatAddr = MARKER_LSAX;

    if (!wait_edge(&t->edge) || !wait_ticks(phase * TICKS)) return 0;
    SPU_KEY_ON_LOW = VOICE_BIT;
    t->kon = now();
    if (mode == WRITE_AFTER_KON || mode == WRITE_FETCHES) SPU_VOICES[VOICE].sampleRepeatAddr = MARKER_LSAX;
    t->endxClear = t->attack = NONE;
    t->fetches = 0;
    int wrote = mode != WRITE_AFTER_ATTACK;
    for (unsigned i = 0; i < POLL_LIMIT; i++) {
        const uint16_t elapsed = now() - t->kon;
        if (t->endxClear == NONE && !(ENDX_LOW & VOICE_BIT)) t->endxClear = elapsed;
        if (t->attack == NONE && SPU_VOICES[VOICE].currentVolume != 0) {
            t->attack = elapsed;
            if (!wrote) {
                SPU_VOICES[VOICE].sampleRepeatAddr = MARKER_LSAX;
                wrote = 1;
            }
        }
        if (mode == WRITE_FETCHES && SPU_VOICES[VOICE].sampleRepeatAddr == SAMPLE_LSAX) {
            t->fetches++;
            SPU_VOICES[VOICE].sampleRepeatAddr = MARKER_LSAX;
        }
        if (elapsed >= 16 * TICKS) break;
    }
    // Block 8 is 224 samples in at this pitch; give it time to be fetched.
    if (!wait_ticks(320 * TICKS)) return 0;
    t->finalLsax = SPU_VOICES[VOICE].sampleRepeatAddr;
    ramsyscall_printf("SPUKONLATCH: phase=%d mode=%d edge->kon=%u endx=%u attack=%u fetches=%u final=%04x\n", phase,
                      mode, (uint16_t)(t->kon - t->edge), t->endxClear, t->attack, t->fetches, t->finalLsax);
    return t->endxClear != NONE && t->attack != NONE;
}

/* Times are in timer ticks after the KEY ON write; 24 ticks is a quarter sample. */
static int near(uint16_t value, unsigned target, unsigned tolerance) {
    return value != NONE && value + tolerance >= target && value <= target + tolerance;
}

int main(void) {
    const int interrupts = enterCriticalSection();
    const uint32_t mask = IMASK;
    IMASK = 0;
    COUNTERS[2].mode = 0x0200;
    DPCR |= 0x000b0000;
    SPU_CTRL = 0;
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_REVERB_LEFT = 0;
    SPU_REVERB_RIGHT = 0;
    SPU_PITCH_MOD_LOW = SPU_PITCH_MOD_HIGH = 0;
    SPU_NOISE_EN_LOW = SPU_NOISE_EN_HIGH = 0;
    SPU_REVERB_EN_LOW = SPU_REVERB_EN_HIGH = 0;
    // With this register unwritten the capture pointer can sit still, and bit 11 with it.
    SPU_RAM_CTRL = 4;
    SPU_CTRL = 0xc000;
    for (unsigned v = 0; v < 24; v++) {
        if (v == VOICE) continue;
        SPU_VOICES[v].sampleRate = 0;
        SPU_VOICES[v].sampleStartAddr = MARKER_ADDR >> 3;
    }

    int ok = upload();
    check(ok, "SPU RAM upload readback");
    Trial early, late, fetch, phase0, phase1;
    if (ok) {
        ok = trial(&early, 0, WRITE_AFTER_KON) && trial(&late, 0, WRITE_AFTER_ATTACK) &&
             trial(&fetch, 0, WRITE_FETCHES) && trial(&phase0, 0, WRITE_NONE) && trial(&phase1, 1, WRITE_NONE);
        check(ok, "a bounded wait or a trial precondition failed");
    }
    if (ok) {
        // A write right after KEY ON is replaced by the next loop-start flag.
        check(early.finalLsax == BLOCK8_LSAX, "write right after key-on held against loop-start");
        // A write after the attack has started holds against block 8's flag.
        check(late.finalLsax == MARKER_LSAX, "write after the attack was replaced by loop-start");
        // Three header fetches copy block 0's address over the marker, then a rewrite holds.
        check(fetch.fetches == 3, "header fetches after key-on are not three");
        check(fetch.finalLsax == MARKER_LSAX, "rewrite after the third fetch did not hold");
        // The first envelope step is four samples after ENDX clears.
        check(near(phase0.attack - phase0.endxClear, 4 * TICKS, 24), "phase 0 attack is not 4 samples after ENDX");
        check(near(phase1.attack - phase1.endxClear, 4 * TICKS, 24), "phase 1 attack is not 4 samples after ENDX");
        // Silicon services KEY ON 1.9 to 3.0 samples after the write.
        check(phase0.endxClear >= TICKS && phase0.endxClear <= 13 * TICKS / 4, "phase 0 key-on service latency");
        check(phase1.endxClear >= TICKS && phase1.endxClear <= 13 * TICKS / 4, "phase 1 key-on service latency");
        // KEY ON is serviced on every other sample: keying on one sample later moves the
        // service by zero or two samples, never one. Which of the two depends on where the
        // capture ring sits against that grid, and that differs with the SPU setup sequence.
        const uint16_t service0 = (uint16_t)(phase0.kon - phase0.edge) + phase0.endxClear;
        const uint16_t service1 = (uint16_t)(phase1.kon - phase1.edge) + phase1.endxClear;
        const int moved = (int16_t)(service1 - service0);
        check((moved >= -24 && moved <= 24) || near((uint16_t)moved, 2 * TICKS, 24),
              "key-on service is not on a two-sample grid");
    }

    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0x00ff;
    SPU_CTRL = 0;
    COUNTERS[2].mode = 0;
    IMASK = mask;
    if (interrupts) leaveCriticalSection();
    ramsyscall_printf("SPUKONLATCH: %s failures=%d\n", ok && !s_failures ? "PASS" : "FAIL", s_failures);
    return ok && !s_failures ? 0 : 1;
}
