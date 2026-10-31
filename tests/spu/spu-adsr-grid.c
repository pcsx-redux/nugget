// ==========================================================================
// ADSR step grid: every voice on a slow rate steps on the same samples
// ==========================================================================
//
// Voice 0 keys on, voice 23 keys on 2 to 21 samples later, both with a slow
// linear attack. Every step voice 23 takes lands on a sample where voice 0
// also steps (voice 23 is serviced later in the sample, so it shows up to
// two polls after). An envelope counter started at each key-on would put
// voice 23's steps the key-on delay after voice 0's instead.

CESTER_BODY(
#define ADSR_GRID_POLL 338u
#define ADSR_GRID_MAX_LAG 700u
#define ADSR_GRID_STEPS_A 24
#define ADSR_GRID_STEPS_B 8

static const uint8_t s_adsrGridSine[64] __attribute__((aligned(4))) = {
    0x00, 0x06, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x00, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x00, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x03, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
};

static void adsr_grid_setup(int v, uint32_t adsr) {
    SPU_VOICES[v].sampleRate = 0x1000;
    SPU_VOICES[v].sampleStartAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[v].sampleRepeatAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[v].volumeLeft = 0;
    SPU_VOICES[v].volumeRight = 0;
    SPU_VOICES[v].adsrLo = (uint16_t)(adsr & 0xffff);
    SPU_VOICES[v].adsrHi = (uint16_t)(adsr >> 16);
}

// Returns how many of voice 23's steps were checked against voice 0's;
// *worst gets the largest lag from a voice 23 step back to the latest voice 0 step.
static int adsr_grid_run(uint32_t adsr, uint32_t delay, uint32_t *worst) {
    uint32_t ta[ADSR_GRID_STEPS_A], tb[ADSR_GRID_STEPS_B];
    int na = 0, nb = 0, bOn = 0;
    uint16_t la = 0, lb = 0;

    SPU_VOICES[0].adsrHi = (uint16_t)(RELEASE(0, 0) >> 16);
    SPU_VOICES[23].adsrHi = (uint16_t)(RELEASE(0, 0) >> 16);
    SPU_KEY_OFF_LOW = 0xffff; SPU_KEY_OFF_HIGH = 0xffff;
    spu_busy_wait(20000);
    adsr_grid_setup(0, adsr);
    adsr_grid_setup(23, adsr);
    SPU_KEY_OFF_LOW = 0; SPU_KEY_OFF_HIGH = 0;
    spu_busy_wait(2000);

    COUNTERS[2].mode = 0;
    uint16_t last = COUNTERS[2].value;
    uint32_t t = 0, next = 0;
    SPU_KEY_ON_LOW = 1;
    while (t < 2000000 && (na < ADSR_GRID_STEPS_A || nb < ADSR_GRID_STEPS_B)) {
        uint16_t now = COUNTERS[2].value;
        t += (uint16_t)(now - last);
        last = now;
        if (!bOn && t >= delay) { SPU_KEY_ON_HIGH = 1u << 7; bOn = 1; }
        if (t >= next) {
            uint16_t a = SPU_VOICES[0].currentVolume, b = SPU_VOICES[23].currentVolume;
            if (a != la && na < ADSR_GRID_STEPS_A) { ta[na++] = t; la = a; }
            if (bOn && b != lb && nb < ADSR_GRID_STEPS_B) { tb[nb++] = t; lb = b; }
            next += ADSR_GRID_POLL;
        }
    }
    SPU_KEY_OFF_LOW = 0xffff; SPU_KEY_OFF_HIGH = 0xffff;

    int checked = 0;
    *worst = 0;
    for (int i = 0; i < nb; i++) {
        if (na == 0 || tb[i] > ta[na - 1] + ADSR_GRID_MAX_LAG) break;
        uint32_t lag = 0xffffffffu;
        for (int j = 0; j < na; j++) {
            if (ta[j] <= tb[i] && tb[i] - ta[j] < lag) lag = tb[i] - ta[j];
        }
        if (lag > *worst) *worst = lag;
        checked++;
    }
    return checked;
}
)

CESTER_TEST(adsr_voices_share_step_grid, spu_tests,
    static const uint32_t delays[] = {1536, 3840, 8448, 16128};
    const uint32_t hold = DECAY(0) | SUSTAIN(3, 0x1f, 15, 0, 0) | RELEASE(0, 0);

    spu_reset_quiet();
    for (int i = 0; i < 64; i++) s_upload[i] = s_adsrGridSine[i];
    for (int i = 64; i < 128; i++) s_upload[i] = 0;
    spu_write_sync(SPU_UPLOAD_ADDR, s_upload, 128);
    SPU_CTRL = 0x8000 | 0x4000;
    SPU_VOL_MAIN_LEFT = 0; SPU_VOL_MAIN_RIGHT = 0;

    for (int shift = 15; shift <= 16; shift++) {
        for (unsigned k = 0; k < sizeof(delays) / sizeof(delays[0]); k++) {
            uint32_t worst;
            int checked = adsr_grid_run(ATTACK(0, shift, 0) | hold, delays[k], &worst);
            if (checked < 4 || worst > ADSR_GRID_MAX_LAG) {
                ramsyscall_printf("adsr grid: shift %d delay %u checked %d worst lag %u\n", shift, delays[k],
                                  checked, worst);
            }
            cester_assert_int_ge(checked, 4);
            cester_assert_uint_le(worst, ADSR_GRID_MAX_LAG);
        }
    }
    muteSpu();
)
