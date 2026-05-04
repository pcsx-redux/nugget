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

// GTE input-register latency probe.
//
// For each (instruction, input register), determine the smallest N such that
// MTC2/CTC2 to that register N nops after the GTE instruction does NOT change
// the GTE's output. The smallest such N is the cycle by which the register
// has been latched into the GTE pipeline - safe to overwrite from then on.

#include "common/hardware/cop2.h"
#include "common/syscalls/syscalls.h"

// clang-format off

#ifndef GTE_LATENCY_HELPERS_DEFINED
#define GTE_LATENCY_HELPERS_DEFINED

#define MAX_N 44

// Canary VXY value: VX=0x4000, VY=0x4000 (max signed half each in low/high
// halves of the 32-bit register). Picks input wildly different from baseline.
#define CANARY_VXY 0x40004000u

// NCCT(sf=1, lm=1) = COP2_OP(17, 1, 0, 0, 0, 1, 0x3f) = 0x0118043f
#define NCCT_OP_SF1_LM1 0x0118043f

static inline void gte_enable(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

static inline uint32_t irq_disable(void) {
    uint32_t sr_orig, sr_new;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr_orig));
    sr_new = sr_orig & ~1u;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr_new));
    return sr_orig;
}

static inline void irq_restore(uint32_t sr) {
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

typedef struct {
    uint32_t rgb0, rgb1, rgb2;
    int32_t  mac1, mac2, mac3;
    uint32_t flag;
} probe_result_t;

// Set up the test scene. LLM and LCM are true diagonal identity (after the
// 12.0 -> 12.12 conversion: 1.0 = 0x1000).
//
// Packing: register LxyLab = (Lab << 16) | Lxy. The diagonal entry goes in
// the LOW half of the appropriate register.
//
// V0 = (0, 0, 0x1000)   normal pointing at the light along +Z (only B)
// V1 = (0x1000, 0, 0)   normal along +X (only R)
// V2 = (0, 0x1000, 0)   normal along +Y (only G)
// LLM, LCM = identity
// BK = FC = 0
// RGBC = (0x80, 0x80, 0x80, 0)
//
// Expected NCCT output:
//   RGB0 = 0x00800000  (V0 -> B=0x80 only)
//   RGB1 = 0x00000080  (V1 -> R=0x80 only)
//   RGB2 = 0x00008000  (V2 -> G=0x80 only)
//   MAC = V2's compute result (0, 0x800, 0)
// Resets every input register the test scene uses to its baseline value.
// We call this before every probe iteration so prior canary writes can't
// leak between tests (perturbation always lands at the same starting state).
static inline void scene_setup(void) {
    // LLM = identity diagonal
    cop2_putc(8,  0x00001000);   // L11L12: L11=0x1000, L12=0
    cop2_putc(9,  0x00000000);   // L13L21
    cop2_putc(10, 0x00001000);   // L22L23: L22=0x1000, L23=0
    cop2_putc(11, 0x00000000);   // L31L32
    cop2_putc(12, 0x1000);       // L33

    // LCM = identity diagonal
    cop2_putc(16, 0x00001000);   // LR1LR2: LR1=0x1000, LR2=0
    cop2_putc(17, 0x00000000);
    cop2_putc(18, 0x00001000);   // LG2LG3: LG2=0x1000, LG3=0
    cop2_putc(19, 0x00000000);
    cop2_putc(20, 0x1000);       // LB3

    cop2_putc(13, 0); cop2_putc(14, 0); cop2_putc(15, 0);   // BK
    cop2_putc(21, 0); cop2_putc(22, 0); cop2_putc(23, 0);   // FC

    cop2_put(0, 0x00000000);                 // VXY0: VX=0, VY=0
    cop2_put(1, 0x00001000);                 // VZ0  = 0x1000
    cop2_put(2, (0x0000u << 16) | 0x1000u);  // VXY1: VX=0x1000, VY=0
    cop2_put(3, 0);                          // VZ1
    cop2_put(4, (0x1000u << 16) | 0x0000u);  // VXY2: VX=0,      VY=0x1000
    cop2_put(5, 0);                          // VZ2
    cop2_put(6, 0x00808080);                 // RGBC
    cop2_putc(31, 0);                        // FLAG
}

static inline int results_equal(const probe_result_t* a, const probe_result_t* b) {
    return a->rgb0 == b->rgb0 && a->rgb1 == b->rgb1 && a->rgb2 == b->rgb2
        && a->mac1 == b->mac1 && a->mac2 == b->mac2 && a->mac3 == b->mac3
        && a->flag == b->flag;
}

static inline void read_full_state(probe_result_t* r) {
    cop2_get(20, r->rgb0);
    cop2_get(21, r->rgb1);
    cop2_get(22, r->rgb2);
    int32_t m1, m2, m3;
    cop2_get(25, m1); cop2_get(26, m2); cop2_get(27, m3);
    r->mac1 = m1; r->mac2 = m2; r->mac3 = m3;
    cop2_getc(31, r->flag);
}

// PROBE_AT_OFFSET(N, target_reg, canary, out): NCCT, N nops, MTC2 canary
// to target_reg, drain. Then caller reads outputs.
#define PROBE_AT_OFFSET(N, target_reg, canary) do {                        \
    __asm__ volatile(                                                      \
        "cop2 %0\n\t"                                                      \
        ".rept " #N "\n\t"                                                 \
        "nop\n\t"                                                          \
        ".endr\n\t"                                                        \
        "mtc2 %1, $" #target_reg "\n\t"                                    \
        ".rept 60\n\t"                                                     \
        "nop\n\t"                                                          \
        ".endr\n\t"                                                        \
        :                                                                  \
        : "i"(NCCT_OP_SF1_LM1), "r"((uint32_t)(canary))                    \
        : "memory");                                                       \
} while (0)

#define PROBE_BASELINE(target_reg, canary) do {                            \
    __asm__ volatile(                                                      \
        "cop2 %0\n\t"                                                      \
        ".rept 80\n\t"                                                     \
        "nop\n\t"                                                          \
        ".endr\n\t"                                                        \
        "mtc2 %1, $" #target_reg "\n\t"                                    \
        ".rept 4\n\t"                                                      \
        "nop\n\t"                                                          \
        ".endr\n\t"                                                        \
        :                                                                  \
        : "i"(NCCT_OP_SF1_LM1), "r"((uint32_t)(canary))                    \
        : "memory");                                                       \
} while (0)

#define PROBE_SANITY_PRE(target_reg, canary) do {                          \
    __asm__ volatile(                                                      \
        "mtc2 %1, $" #target_reg "\n\t"                                    \
        "nop\n\tnop\n\t"                                                   \
        "cop2 %0\n\t"                                                      \
        ".rept 80\n\t"                                                     \
        "nop\n\t"                                                          \
        ".endr\n\t"                                                        \
        :                                                                  \
        : "i"(NCCT_OP_SF1_LM1), "r"((uint32_t)(canary))                    \
        : "memory");                                                       \
} while (0)

#define DO_SWEEP(reg, canary, results)                                     \
    do {                                                                   \
        scene_setup(); PROBE_AT_OFFSET( 0, reg, canary); read_full_state(&(results)[ 0]); \
        scene_setup(); PROBE_AT_OFFSET( 1, reg, canary); read_full_state(&(results)[ 1]); \
        scene_setup(); PROBE_AT_OFFSET( 2, reg, canary); read_full_state(&(results)[ 2]); \
        scene_setup(); PROBE_AT_OFFSET( 3, reg, canary); read_full_state(&(results)[ 3]); \
        scene_setup(); PROBE_AT_OFFSET( 4, reg, canary); read_full_state(&(results)[ 4]); \
        scene_setup(); PROBE_AT_OFFSET( 5, reg, canary); read_full_state(&(results)[ 5]); \
        scene_setup(); PROBE_AT_OFFSET( 6, reg, canary); read_full_state(&(results)[ 6]); \
        scene_setup(); PROBE_AT_OFFSET( 7, reg, canary); read_full_state(&(results)[ 7]); \
        scene_setup(); PROBE_AT_OFFSET( 8, reg, canary); read_full_state(&(results)[ 8]); \
        scene_setup(); PROBE_AT_OFFSET( 9, reg, canary); read_full_state(&(results)[ 9]); \
        scene_setup(); PROBE_AT_OFFSET(10, reg, canary); read_full_state(&(results)[10]); \
        scene_setup(); PROBE_AT_OFFSET(11, reg, canary); read_full_state(&(results)[11]); \
        scene_setup(); PROBE_AT_OFFSET(12, reg, canary); read_full_state(&(results)[12]); \
        scene_setup(); PROBE_AT_OFFSET(13, reg, canary); read_full_state(&(results)[13]); \
        scene_setup(); PROBE_AT_OFFSET(14, reg, canary); read_full_state(&(results)[14]); \
        scene_setup(); PROBE_AT_OFFSET(15, reg, canary); read_full_state(&(results)[15]); \
        scene_setup(); PROBE_AT_OFFSET(16, reg, canary); read_full_state(&(results)[16]); \
        scene_setup(); PROBE_AT_OFFSET(17, reg, canary); read_full_state(&(results)[17]); \
        scene_setup(); PROBE_AT_OFFSET(18, reg, canary); read_full_state(&(results)[18]); \
        scene_setup(); PROBE_AT_OFFSET(19, reg, canary); read_full_state(&(results)[19]); \
        scene_setup(); PROBE_AT_OFFSET(20, reg, canary); read_full_state(&(results)[20]); \
        scene_setup(); PROBE_AT_OFFSET(21, reg, canary); read_full_state(&(results)[21]); \
        scene_setup(); PROBE_AT_OFFSET(22, reg, canary); read_full_state(&(results)[22]); \
        scene_setup(); PROBE_AT_OFFSET(23, reg, canary); read_full_state(&(results)[23]); \
        scene_setup(); PROBE_AT_OFFSET(24, reg, canary); read_full_state(&(results)[24]); \
        scene_setup(); PROBE_AT_OFFSET(25, reg, canary); read_full_state(&(results)[25]); \
        scene_setup(); PROBE_AT_OFFSET(26, reg, canary); read_full_state(&(results)[26]); \
        scene_setup(); PROBE_AT_OFFSET(27, reg, canary); read_full_state(&(results)[27]); \
        scene_setup(); PROBE_AT_OFFSET(28, reg, canary); read_full_state(&(results)[28]); \
        scene_setup(); PROBE_AT_OFFSET(29, reg, canary); read_full_state(&(results)[29]); \
        scene_setup(); PROBE_AT_OFFSET(30, reg, canary); read_full_state(&(results)[30]); \
        scene_setup(); PROBE_AT_OFFSET(31, reg, canary); read_full_state(&(results)[31]); \
        scene_setup(); PROBE_AT_OFFSET(32, reg, canary); read_full_state(&(results)[32]); \
        scene_setup(); PROBE_AT_OFFSET(33, reg, canary); read_full_state(&(results)[33]); \
        scene_setup(); PROBE_AT_OFFSET(34, reg, canary); read_full_state(&(results)[34]); \
        scene_setup(); PROBE_AT_OFFSET(35, reg, canary); read_full_state(&(results)[35]); \
        scene_setup(); PROBE_AT_OFFSET(36, reg, canary); read_full_state(&(results)[36]); \
        scene_setup(); PROBE_AT_OFFSET(37, reg, canary); read_full_state(&(results)[37]); \
        scene_setup(); PROBE_AT_OFFSET(38, reg, canary); read_full_state(&(results)[38]); \
        scene_setup(); PROBE_AT_OFFSET(39, reg, canary); read_full_state(&(results)[39]); \
        scene_setup(); PROBE_AT_OFFSET(40, reg, canary); read_full_state(&(results)[40]); \
        scene_setup(); PROBE_AT_OFFSET(41, reg, canary); read_full_state(&(results)[41]); \
        scene_setup(); PROBE_AT_OFFSET(42, reg, canary); read_full_state(&(results)[42]); \
        scene_setup(); PROBE_AT_OFFSET(43, reg, canary); read_full_state(&(results)[43]); \
        scene_setup(); PROBE_AT_OFFSET(44, reg, canary); read_full_state(&(results)[44]); \
    } while (0)

static void report_sweep(const char* name,
                         const probe_result_t* baseline,
                         const probe_result_t* sanity_pre,
                         const probe_result_t results[MAX_N + 1]) {
    ramsyscall_printf("=== %s ===\n", name);
    ramsyscall_printf("baseline    RGB=(0x%08x,0x%08x,0x%08x) MAC=(%d,%d,%d) FLAG=0x%08x\n",
                      baseline->rgb0, baseline->rgb1, baseline->rgb2,
                      baseline->mac1, baseline->mac2, baseline->mac3, baseline->flag);
    int sanity_differs = !results_equal(baseline, sanity_pre);
    ramsyscall_printf("sanity-pre  RGB=(0x%08x,0x%08x,0x%08x) MAC=(%d,%d,%d) FLAG=0x%08x %s\n",
                      sanity_pre->rgb0, sanity_pre->rgb1, sanity_pre->rgb2,
                      sanity_pre->mac1, sanity_pre->mac2, sanity_pre->mac3, sanity_pre->flag,
                      sanity_differs ? "DIFFERS_OK" : "*** SAME_AS_BASELINE_BUG ***");
    int boundary = -1;
    for (int n = 0; n <= MAX_N; n++) {
        int matches = results_equal(baseline, &results[n]);
        ramsyscall_printf("N=%2d RGB=(0x%08x,0x%08x,0x%08x) MAC=(%d,%d,%d) FLAG=0x%08x %s\n",
                          n,
                          results[n].rgb0, results[n].rgb1, results[n].rgb2,
                          results[n].mac1, results[n].mac2, results[n].mac3,
                          results[n].flag,
                          matches ? "MATCH" : "diff");
        if (matches && boundary < 0) boundary = n;
    }
    ramsyscall_printf("=== %s boundary: N=%d ===\n", name, boundary);
}

#endif // GTE_LATENCY_HELPERS_DEFINED

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

CESTER_BEFORE_ALL(gte_latency_tests,
    gte_enable();
)

// ==========================================================================
// Smoke test: verify the test scene gives the math we expect.
// ==========================================================================
CESTER_TEST(ncct_scene_smoke, gte_latency_tests,
    scene_setup();
    cop2_cmd(COP2_NCCT(1, 1));
    probe_result_t r;
    read_full_state(&r);
    ramsyscall_printf("smoke NCCT: RGB=(0x%08x,0x%08x,0x%08x) MAC=(%d,%d,%d) FLAG=0x%08x\n",
                      r.rgb0, r.rgb1, r.rgb2, r.mac1, r.mac2, r.mac3, r.flag);
    // Expected:
    //   RGB0 = 0x00800000 (V0 -> B=0x80)
    //   RGB1 = 0x00000080 (V1 -> R=0x80)
    //   RGB2 = 0x00008000 (V2 -> G=0x80)
    //   MAC  = (0, 0x800, 0) - last sub-pass (V2)'s color stage
    cester_assert_uint_eq(0x00800000, r.rgb0);
    cester_assert_uint_eq(0x00000080, r.rgb1);
    cester_assert_uint_eq(0x00008000, r.rgb2);
    cester_assert_int_eq(0,     r.mac1);
    cester_assert_int_eq(0x800, r.mac2);
    cester_assert_int_eq(0,     r.mac3);
)

// ==========================================================================
// VXY0/VXY1/VXY2 latency probes (data registers 0, 2, 4 - X+Y packed)
// ==========================================================================
#define MAKE_VXY_TEST(suffix, reg)                                         \
CESTER_TEST(ncct_vxy##suffix##_latency, gte_latency_tests,                 \
    static probe_result_t baseline, sanity_pre;                            \
    static probe_result_t warmup[MAX_N + 1];                               \
    static probe_result_t results[MAX_N + 1];                              \
    scene_setup();                                                         \
    PROBE_BASELINE(reg, CANARY_VXY); read_full_state(&baseline);           \
    scene_setup();                                                         \
    PROBE_SANITY_PRE(reg, CANARY_VXY); read_full_state(&sanity_pre);       \
    uint32_t saved_sr = irq_disable();                                     \
    DO_SWEEP(reg, CANARY_VXY, warmup);                                     \
    DO_SWEEP(reg, CANARY_VXY, results);                                    \
    irq_restore(saved_sr);                                                 \
    report_sweep("NCCT VXY" #suffix " (V" #suffix ")",                     \
                 &baseline, &sanity_pre, results);                         \
    cester_assert_true(!results_equal(&baseline, &sanity_pre));            \
)

MAKE_VXY_TEST(0, 0)
MAKE_VXY_TEST(1, 2)
MAKE_VXY_TEST(2, 4)

// ==========================================================================
// VZ0/VZ1/VZ2 latency probes (data registers 1, 3, 5)
// ==========================================================================
// Canary VZ = 0x4000 (different from baseline 0x1000 / 0x0000).

#define CANARY_VZ 0x00004000u

#define MAKE_VZ_TEST(suffix, reg)                                          \
CESTER_TEST(ncct_vz##suffix##_latency, gte_latency_tests,                  \
    static probe_result_t baseline, sanity_pre;                            \
    static probe_result_t warmup[MAX_N + 1];                               \
    static probe_result_t results[MAX_N + 1];                              \
    scene_setup();                                                         \
    PROBE_BASELINE(reg, CANARY_VZ); read_full_state(&baseline);            \
    scene_setup();                                                         \
    PROBE_SANITY_PRE(reg, CANARY_VZ); read_full_state(&sanity_pre);        \
    uint32_t saved_sr = irq_disable();                                     \
    DO_SWEEP(reg, CANARY_VZ, warmup);                                      \
    DO_SWEEP(reg, CANARY_VZ, results);                                     \
    irq_restore(saved_sr);                                                 \
    report_sweep("NCCT VZ" #suffix " (V" #suffix ")",                      \
                 &baseline, &sanity_pre, results);                         \
    cester_assert_true(!results_equal(&baseline, &sanity_pre));            \
)

MAKE_VZ_TEST(0, 1)
MAKE_VZ_TEST(1, 3)
MAKE_VZ_TEST(2, 5)

// ==========================================================================
// RGBC latency probe (data register 6)
// RGBC affects all 3 sub-passes (vertex color tint at the color stage).
// ==========================================================================
#define CANARY_RGBC 0x00404040u   // distinct color from baseline 0x80808080

CESTER_TEST(ncct_rgbc_latency, gte_latency_tests,
    static probe_result_t baseline, sanity_pre;
    static probe_result_t warmup[MAX_N + 1];
    static probe_result_t results[MAX_N + 1];
    scene_setup();
    PROBE_BASELINE(6, CANARY_RGBC); read_full_state(&baseline);
    scene_setup();
    PROBE_SANITY_PRE(6, CANARY_RGBC); read_full_state(&sanity_pre);
    uint32_t saved_sr = irq_disable();
    DO_SWEEP(6, CANARY_RGBC, warmup);
    DO_SWEEP(6, CANARY_RGBC, results);
    irq_restore(saved_sr);
    report_sweep("NCCT RGBC", &baseline, &sanity_pre, results);
    cester_assert_true(!results_equal(&baseline, &sanity_pre));
)
