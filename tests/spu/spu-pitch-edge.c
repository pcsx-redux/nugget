// ============================================================================
// Characterize SPU pitch counter, Gaussian interpolation, clipping, and PMON
// pitch-modulation edge cases. Goldens are hardware-owned: run with
// SPU_DUMP=true on silicon to collect .test.pcm files, then enable
// SPU_PITCH_EDGE_GOLDENS=true once those captures have been checked in.
// ============================================================================

#ifndef SPU_PITCH_EDGE_GOLDENS_AVAILABLE
#define SPU_PITCH_EDGE_GOLDENS_AVAILABLE 0
#endif

#if SPU_PITCH_EDGE_GOLDENS_AVAILABLE && !defined(SPU_DUMP)
// cester re-includes this file in contexts where a file-scope asm() is not
// legal, so the incbins have to ride CESTER_BODY like spu-adpcm-edge.c's.
CESTER_BODY(
INCLUDE_PCM(pitch_edge_0000_stop);
INCLUDE_PCM(pitch_edge_0001_substep);
INCLUDE_PCM(pitch_edge_0010_gauss_index);
INCLUDE_PCM(pitch_edge_0100_slow_tail);
INCLUDE_PCM(pitch_edge_0fff_fractional);
INCLUDE_PCM(pitch_edge_1000_unity);
INCLUDE_PCM(pitch_edge_1001_fractional);
INCLUDE_PCM(pitch_edge_3fff_preclip);
INCLUDE_PCM(pitch_edge_4000_clip);
INCLUDE_PCM(pitch_edge_4001_clip);
INCLUDE_PCM(pitch_edge_7fff_clip);
INCLUDE_PCM(pitch_edge_8000_signed);
INCLUDE_PCM(pitch_edge_ffff_signed);
INCLUDE_PCM(pitch_edge_fmod_disabled_control);
INCLUDE_PCM(pitch_edge_fmod_sine_sweep);
INCLUDE_PCM(pitch_edge_fmod_square_edges);
INCLUDE_PCM(pitch_edge_fmod_high_pitch_clip);
)
#endif

CESTER_BODY(
static uint32_t spu_pitch_edge_hash_capture(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 512; i++) {
        h ^= (uint16_t)s_capture[i];
        h *= 16777619u;
    }
    return h;
}

static void spu_pitch_edge_observe(const char *name) {
    int16_t min = (int16_t)s_capture[0];
    int16_t max = (int16_t)s_capture[0];
    unsigned changes = 0;
    for (int i = 1; i < 512; i++) {
        int16_t v = (int16_t)s_capture[i];
        if (v < min) min = v;
        if (v > max) max = v;
        if (s_capture[i] != s_capture[i - 1]) changes++;
    }
    ramsyscall_printf("OBS spu_pitch_edge golden %s.test.pcm hash=0x%08x "
                      "first=0x%04x mid=0x%04x last=0x%04x "
                      "min=0x%04x max=0x%04x changes=%u\n",
                      name, spu_pitch_edge_hash_capture(), s_capture[0],
                      s_capture[256], s_capture[511], (uint16_t)min,
                      (uint16_t)max, changes);
}

static void spu_pitch_edge_copy_sample(uint32_t spuAddr, const uint8_t *sample64) {
    for (int i = 0; i < 64; i++) s_upload[i] = sample64[i];
    for (int i = 64; i < 128; i++) s_upload[i] = 0xaa;
    spu_write_sync(spuAddr, s_upload, 128);
}

static void spu_pitch_edge_setup_voice(int voice, uint32_t spuAddr,
                                       uint16_t pitch, uint16_t volume) {
    SPU_VOICES[voice].volumeLeft = volume;
    SPU_VOICES[voice].volumeRight = volume;
    SPU_VOICES[voice].sampleRate = pitch;
    SPU_VOICES[voice].sampleStartAddr = spuAddr >> 3;
    SPU_VOICES[voice].sampleRepeatAddr = spuAddr >> 3;
    SPU_VOICES[voice].adsrLo = 0x000f;   // instant attack, sustain level=0xF
    SPU_VOICES[voice].adsrHi = 0x1fc0;   // sustain rate=0x7F, increase, linear
}

static void spu_pitch_edge_run_fmod(const uint8_t *modSample64,
                                    uint16_t modPitch,
                                    const uint8_t *carrierSample64,
                                    uint16_t carrierPitch,
                                    int enablePmon) {
    const uint32_t modAddr = SPU_UPLOAD_ADDR;
    const uint32_t carrierAddr = SPU_UPLOAD_ADDR + 0x100;
    spu_reset_quiet();
    spu_pitch_edge_copy_sample(modAddr, modSample64);
    spu_pitch_edge_copy_sample(carrierAddr, carrierSample64);
    SPU_CTRL = 0x8000 | 0x4000;
    SPU_VOL_MAIN_LEFT = 0x3fff;
    SPU_VOL_MAIN_RIGHT = 0x3fff;
    SPU_PITCH_MOD_LOW = enablePmon ? (1u << 1) : 0;
    SPU_PITCH_MOD_HIGH = 0;

    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    spu_busy_wait(800000);

    spu_wait_status_bit11_flip();
    spu_pitch_edge_setup_voice(0, modAddr, modPitch, 0);
    spu_pitch_edge_setup_voice(1, carrierAddr, carrierPitch, 0);
    SPU_KEY_OFF_LOW = 0;
    SPU_KEY_OFF_HIGH = 0;
    SPU_KEY_ON_LOW = (1u << 0) | (1u << 1);
    spu_wait_status_bit11_flip();

    spu_read_sync(0x0800, s_capture, 1024);
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_PITCH_MOD_LOW = 0;
    muteSpu();
}
)

#ifdef SPU_DUMP
#define SPU_PITCH_EDGE_ASSERT_GOLDEN(name) spu_dump_pcm(#name ".test.pcm", s_capture, 1024)
#elif SPU_PITCH_EDGE_GOLDENS_AVAILABLE
#define SPU_PITCH_EDGE_ASSERT_GOLDEN(name) \
    do { \
        extern const uint8_t name[]; \
        cester_assert_int_eq(0, spu_compare_golden(#name, s_capture, name)); \
    } while (0)
#else
#define SPU_PITCH_EDGE_ASSERT_GOLDEN(name) spu_pitch_edge_observe(#name)
#endif

#define SPU_PITCH_EDGE_TEST(NAME, PITCH) \
CESTER_TEST(pitch_edge_##NAME, spu_tests, \
    run_voice1_with_sample(kAdpcmTriangle, (PITCH)); \
    SPU_PITCH_EDGE_ASSERT_GOLDEN(pitch_edge_##NAME); \
)

SPU_PITCH_EDGE_TEST(0000_stop, 0x0000)
SPU_PITCH_EDGE_TEST(0001_substep, 0x0001)
SPU_PITCH_EDGE_TEST(0010_gauss_index, 0x0010)
SPU_PITCH_EDGE_TEST(0100_slow_tail, 0x0100)
SPU_PITCH_EDGE_TEST(0fff_fractional, 0x0fff)
SPU_PITCH_EDGE_TEST(1000_unity, 0x1000)
SPU_PITCH_EDGE_TEST(1001_fractional, 0x1001)
SPU_PITCH_EDGE_TEST(3fff_preclip, 0x3fff)
SPU_PITCH_EDGE_TEST(4000_clip, 0x4000)
SPU_PITCH_EDGE_TEST(4001_clip, 0x4001)
SPU_PITCH_EDGE_TEST(7fff_clip, 0x7fff)
SPU_PITCH_EDGE_TEST(8000_signed, 0x8000)
SPU_PITCH_EDGE_TEST(ffff_signed, 0xffff)

CESTER_TEST(pitch_edge_fmod_disabled_control, spu_tests,
    spu_pitch_edge_run_fmod(kAdpcmSine394Hz, 0x0400, kAdpcmSine, 0x1000, 0);
    SPU_PITCH_EDGE_ASSERT_GOLDEN(pitch_edge_fmod_disabled_control);
)

CESTER_TEST(pitch_edge_fmod_sine_sweep, spu_tests,
    spu_pitch_edge_run_fmod(kAdpcmSine394Hz, 0x0400, kAdpcmSine, 0x1000, 1);
    SPU_PITCH_EDGE_ASSERT_GOLDEN(pitch_edge_fmod_sine_sweep);
)

CESTER_TEST(pitch_edge_fmod_square_edges, spu_tests,
    spu_pitch_edge_run_fmod(kAdpcmSquare, 0x1000, kAdpcmSine, 0x2000, 1);
    SPU_PITCH_EDGE_ASSERT_GOLDEN(pitch_edge_fmod_square_edges);
)

CESTER_TEST(pitch_edge_fmod_high_pitch_clip, spu_tests,
    spu_pitch_edge_run_fmod(kAdpcmSquare, 0x1000, kAdpcmSine, 0x4000, 1);
    SPU_PITCH_EDGE_ASSERT_GOLDEN(pitch_edge_fmod_high_pitch_clip);
)
