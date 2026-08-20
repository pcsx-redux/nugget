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

/* GTE vs SoftMath throughput benchmark.
 *
 * Question: per operation, at what batch size N does routing through the GTE
 * beat psyqo::SoftMath's CPU implementation, in cycles per element, once the
 * mtc2/ctc2/mfc2 round trips and the hazard nops are counted?
 *
 * Four arms per operation, all over the same input array, all timed the same
 * way:
 *
 *   ctl  - the loop and its loads/stores, no math. Instrument check and the
 *          floor every other arm pays. Subtracting it gives the marginal cost
 *          of the math itself.
 *   cpu  - psyqo::SoftMath, unmodified.
 *   cold - GTE with the matrix / config upload INSIDE the loop, once per
 *          element. This is what a stateless free function
 *          (GteMath::matrixVecMul3(m, v, out)) actually costs a caller.
 *   hot  - GTE with the upload hoisted OUT of the timed loop. This is what a
 *          scoped/batched API costs: load once, stream vectors.
 *   hotU - same as hot but with psyqo::GTE::Unsafe register writes (no hazard
 *          nops). Measures what the safety nops cost, and the correctness dump
 *          says whether Unsafe is actually correct here.
 *
 * cold and hot are both linear in N with no setup term (cold folds the setup
 * into the per-element cost, hot excludes it entirely), so the setup cost is
 * recovered as S = (cold(N) - hot(N)) / N and the crossover against the CPU is
 * N* = S / (cpu_per_element - hot_per_element). Both are printed.
 *
 * Cycle source: root counter 2 in system-clock mode (1 tick / CPU cycle,
 * 16-bit), via the COUNTERS macro, same as src/mips/tests/load-timings. IRQs
 * masked across every timed region; minimum taken over 8 runs to reject stray
 * stalls and ensure a warmed icache.
 *
 * THE 16-BIT WRAP IS LOAD-BEARING. A single bracketed region can only measure
 * 65535 cycles. Each arm is probed at N=1 first and the sweep is truncated to
 * the largest N that stays under a 55000-cycle ceiling; anything past that
 * prints "wrap" rather than a wrong number. Every printed delta is also
 * range-checked.
 *
 * This is a hardware-timing test: the emulator does not model GTE latency or
 * memory access costs, so the numbers only mean something on silicon. Under
 * PCSX_TESTS the benchmark is skipped and only the correctness dump runs -
 * that part IS meaningful on the emulator and is how the arms get validated
 * before a hardware run.
 */

#include "common/hardware/counters.h"
#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/matrix.hh"
#include "psyqo/soft-math.hh"
#include "psyqo/vector.hh"

using namespace psyqo;
using namespace psyqo::fixed_point_literals;
namespace GTE = psyqo::GTE;
namespace K = psyqo::GTE::Kernels;

// ==========================================================================
// COP2 enable
// ==========================================================================
//
// psyqo::Application::run() sets CU2 for you. This benchmark deliberately does
// not use Application (run() never returns and blocks on VBlank), so CP0.SR
// bit 30 has to be set by hand. Without it every cop2 instruction is a no-op
// and every GTE arm silently reads zeros - which is exactly what the first
// emulator run showed, and is why the correctness dump exists.
static inline void gteEnable(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

// ==========================================================================
// Data
// ==========================================================================

// 256 elements is the largest batch in the sweep. Vec3 is 12 bytes, so the
// input array is 3 KiB - main RAM, not scratchpad. That is deliberate: real
// vertex data lives in RAM and every arm pays the same load cost, which the
// ctl arm measures.
static constexpr unsigned N_MAX = 256;
static constexpr unsigned N_MAT = 32;  // matrix-multiply batch cap (Matrix33 is 36 bytes)

static Vec3 s_vin[N_MAX];
static Vec3 s_vout[N_MAX];
static Vec2 s_v2out[N_MAX];
static Vec3 s_vnorm[N_MAX];  // scratch for the normalize arms (they mutate in place)
static Matrix33 s_min[N_MAT];
static Matrix33 s_mout[N_MAT];
static Matrix33 s_mat;   // the transform matrix, uploaded to RT
static Matrix33 s_matB;  // second operand for multiplyMatrix33

static volatile uint32_t s_sink;

static uint32_t s_rng = 0x13579bdfu;
static uint32_t nextRand() {
    // xorshift32, deterministic so a rerun measures the same data.
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

// Raw 12.12 in [-lim, lim).
static FixedPoint<> randFp(int32_t lim) {
    int32_t r = (int32_t)(nextRand() % (uint32_t)(2 * lim)) - lim;
    return FixedPoint<>(r, FixedPoint<>::RAW);
}

static void initData() {
    for (unsigned i = 0; i < N_MAX; i++) {
        // x, y in +-0.4; z in [0.15, 0.55], STRICTLY POSITIVE. Three separate
        // constraints, all measured rather than assumed - the first emulator
        // run had z going negative and RTPS clamped SZ3 to 0:
        //   - |v|^2 <= 0.16+0.16+0.3025 = 0.62 < 1, so fastNormalizeVec3 never
        //     takes its `if (x > 1)` divide branch and every element costs the
        //     same four multiply-only Newton steps.
        //   - z > 0 and VZ0 = z.raw() >= 0x266 = 614, well above H/2 = 80, so
        //     RTPS's perspective divide never overflows.
        //   - IR magnitudes stay far from the 16-bit saturation limits, so the
        //     GTE FLAG stays clean and nothing measures the saturation path.
        s_vin[i].x = randFp(0x666);
        s_vin[i].y = randFp(0x666);
        s_vin[i].z = FixedPoint<>((int32_t)(0x266 + (nextRand() % 0x666u)), FixedPoint<>::RAW);
    }
    for (unsigned i = 0; i < N_MAT; i++) {
        for (unsigned r = 0; r < 3; r++) {
            s_min[i].vs[r].x = randFp(0xc00);
            s_min[i].vs[r].y = randFp(0xc00);
            s_min[i].vs[r].z = randFp(0xc00);
        }
    }
    // A well-conditioned rotation-ish matrix, entries inside +-1.0 so the
    // 16-bit GTE matrix registers hold them without truncation loss beyond
    // the format's own.
    s_mat.vs[0].x = 0.866_fp;  s_mat.vs[0].y = -0.5_fp;   s_mat.vs[0].z = 0.0_fp;
    s_mat.vs[1].x = 0.5_fp;    s_mat.vs[1].y = 0.866_fp;  s_mat.vs[1].z = 0.0_fp;
    s_mat.vs[2].x = 0.0_fp;    s_mat.vs[2].y = 0.0_fp;    s_mat.vs[2].z = 1.0_fp;
    s_matB.vs[0].x = 0.707_fp; s_matB.vs[0].y = 0.0_fp;   s_matB.vs[0].z = -0.707_fp;
    s_matB.vs[1].x = 0.0_fp;   s_matB.vs[1].y = 1.0_fp;   s_matB.vs[1].z = 0.0_fp;
    s_matB.vs[2].x = 0.707_fp; s_matB.vs[2].y = 0.0_fp;   s_matB.vs[2].z = 0.707_fp;
}

static void resetNormScratch(unsigned n) {
    for (unsigned i = 0; i < n; i++) s_vnorm[i] = s_vin[i];
}

// ==========================================================================
// Timing
// ==========================================================================

#define TIME_START() uint16_t _t_before = COUNTERS[2].value
#define TIME_END() ((uint32_t)(uint16_t)(COUNTERS[2].value - _t_before))

// Anything at or above this is treated as untrustworthy: the 16-bit counter
// wraps at 65536, and a delta close to that cannot be told apart from a
// wrapped one.
static constexpr uint32_t WRAP_CEILING = 55000;
static constexpr uint32_t NO_RESULT = 0xffffffffu;

typedef uint32_t (*bench_fn)(unsigned n);

// Minimum over 8 runs: warms the icache and rejects stray stalls. Returns
// NO_RESULT if even the best run is close enough to the wrap to be a lie.
static uint32_t bench(bench_fn fn, unsigned n) {
    uint32_t best = 0xffffu;
    for (int i = 0; i < 8; i++) {
        uint32_t d = fn(n);
        if (d < best) best = d;
    }
    if (best >= WRAP_CEILING) return NO_RESULT;
    return best;
}

// ==========================================================================
// GTE register helpers
// ==========================================================================

static inline GTE::Short toShort(FixedPoint<> v) { return GTE::Short((int16_t)v.raw(), GTE::Short::RAW); }

// psyqo::GTE::write is overloaded on uint32_t and const uint32_t*, so a literal
// zero is ambiguous (it is a null pointer constant). Funnel raw writes through
// this trampoline: inside it the argument is a variable, never a literal.
template <GTE::Register reg, GTE::Safety safety = GTE::Safe>
static inline void writeRaw(uint32_t v) {
    GTE::write<reg, safety>(v);
}

// RTPS projection configuration. H is the projection plane distance, OFX/OFY
// the screen offset, DQA/DQB the depth-cue terms. Written raw so the values
// are exactly what the GTE spec wants rather than a fixed-point reinterp.
static constexpr uint32_t RTPS_H = 160;

static inline void uploadRtpsConfig() {
    GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
    writeRaw<GTE::Register::TRX, GTE::Unsafe>(0u);
    writeRaw<GTE::Register::TRY, GTE::Unsafe>(0u);
    writeRaw<GTE::Register::TRZ, GTE::Unsafe>(0u);
    writeRaw<GTE::Register::H, GTE::Unsafe>(RTPS_H);
    writeRaw<GTE::Register::OFX, GTE::Unsafe>(0u);
    writeRaw<GTE::Register::OFY, GTE::Unsafe>(0u);
    writeRaw<GTE::Register::DQA, GTE::Unsafe>(0u);
    writeRaw<GTE::Register::DQB, GTE::Safe>(0u);
}

// ==========================================================================
// matrixVecMul3 / MVMVA
// ==========================================================================

static uint32_t mv_ctl(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        Vec3 v = s_vin[i];
        __asm__ volatile("" : "+r"(v.x.value), "+r"(v.y.value), "+r"(v.z.value));
        s_vout[i] = v;
    }
    return TIME_END();
}

static uint32_t mv_cpu(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        SoftMath::matrixVecMul3(s_mat, s_vin[i], &s_vout[i]);
    }
    return TIME_END();
}

static uint32_t mv_cold(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_vout[i]);
    }
    return TIME_END();
}

static uint32_t mv_hot(unsigned n) {
    GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_vout[i]);
    }
    return TIME_END();
}

static uint32_t mv_hotU(unsigned n) {
    GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        GTE::writeUnsafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_vout[i]);
    }
    return TIME_END();
}

// ==========================================================================
// matrixVecMul3xy / MVMVA, reading only x and y
// ==========================================================================

static uint32_t mvxy_cpu(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        SoftMath::matrixVecMul3xy(s_mat, s_vin[i], &s_v2out[i]);
    }
    return TIME_END();
}

static uint32_t mvxy_cold(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::Register::MAC1>((uint32_t *)&s_v2out[i].x.value);
        GTE::read<GTE::Register::MAC2>((uint32_t *)&s_v2out[i].y.value);
    }
    return TIME_END();
}

static uint32_t mvxy_hot(unsigned n) {
    GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::Register::MAC1>((uint32_t *)&s_v2out[i].x.value);
        GTE::read<GTE::Register::MAC2>((uint32_t *)&s_v2out[i].y.value);
    }
    return TIME_END();
}

// ==========================================================================
// crossProductVec3 / CP (Sony's "OP")
// ==========================================================================
//
// CP computes MAC1 = D2*IR3 - D3*IR2 etc, where D1/D2/D3 are the DIAGONAL of
// the rotation matrix (R11, R22, R33) and IR1..IR3 is the second vector. So
// the first operand goes into three packed control registers and the second
// into three data registers - six writes for one cross product. R12/R23 are
// not read by CP, so the packed halves can carry anything.

static inline void cpWriteDiag(const Vec3 &v) {
    writeRaw<GTE::Register::R11R12, GTE::Unsafe>((uint32_t)(uint16_t)(int16_t)v.x.raw());
    writeRaw<GTE::Register::R22R23, GTE::Unsafe>((uint32_t)(uint16_t)(int16_t)v.y.raw());
    writeRaw<GTE::Register::R33, GTE::Unsafe>((uint32_t)(uint16_t)(int16_t)v.z.raw());
}

static inline void cpWriteIr(const Vec3 &v) {
    GTE::writeUnsafe<GTE::Register::IR1>(toShort(v.x));
    GTE::writeUnsafe<GTE::Register::IR2>(toShort(v.y));
    GTE::writeSafe<GTE::Register::IR3>(toShort(v.z));
}

static uint32_t cp_cpu(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        SoftMath::crossProductVec3(s_mat.vs[0], s_vin[i], &s_vout[i]);
    }
    return TIME_END();
}

// COLD: both operands uploaded per element - the honest cost of
// GteMath::crossProductVec3(a, b, out).
static uint32_t cp_cold(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        cpWriteDiag(s_mat.vs[0]);
        cpWriteIr(s_vin[i]);
        K::cp();
        GTE::read<GTE::PseudoRegister::LV>(s_vout[i]);
    }
    return TIME_END();
}

// HOT: first operand pinned in the diagonal, only the second streams.
static uint32_t cp_hot(unsigned n) {
    cpWriteDiag(s_mat.vs[0]);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        cpWriteIr(s_vin[i]);
        K::cp();
        GTE::read<GTE::PseudoRegister::LV>(s_vout[i]);
    }
    return TIME_END();
}

// ==========================================================================
// project / RTPS
// ==========================================================================
//
// NOT a like-for-like numeric comparison and it is not pretending to be:
// SoftMath::project does one 12.12 divide and two multiplies, while RTPS also
// applies the rotation and translation and emits 11-bit saturated screen
// coordinates. The number that matters here is the cost of getting a
// perspective divide done by each route; the correctness dump prints both so
// the semantic gap is visible rather than assumed away.

static constexpr FixedPoint<> PROJ_H = 1.0_fp;

static uint32_t proj_ctl(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        Vec3 v = s_vin[i];
        __asm__ volatile("" : "+r"(v.x.value), "+r"(v.y.value), "+r"(v.z.value));
        s_v2out[i].x = v.x;
        s_v2out[i].y = v.y;
    }
    return TIME_END();
}

static uint32_t proj_cpu(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        SoftMath::project(&s_vin[i], PROJ_H, &s_v2out[i]);
    }
    return TIME_END();
}

static uint32_t proj_cold(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        uploadRtpsConfig();
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::rtps();
        GTE::read<GTE::Register::SXY2>((uint32_t *)&s_v2out[i].x.value);
        GTE::read<GTE::Register::SZ3>((uint32_t *)&s_v2out[i].y.value);
    }
    return TIME_END();
}

static uint32_t proj_hot(unsigned n) {
    uploadRtpsConfig();
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::rtps();
        GTE::read<GTE::Register::SXY2>((uint32_t *)&s_v2out[i].x.value);
        GTE::read<GTE::Register::SZ3>((uint32_t *)&s_v2out[i].y.value);
    }
    return TIME_END();
}

static uint32_t proj_hotU(unsigned n) {
    uploadRtpsConfig();
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        GTE::writeUnsafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::rtps();
        GTE::read<GTE::Register::SXY2>((uint32_t *)&s_v2out[i].x.value);
        GTE::read<GTE::Register::SZ3>((uint32_t *)&s_v2out[i].y.value);
    }
    return TIME_END();
}

// ==========================================================================
// normalizeVec3 / fastNormalizeVec3 vs a GTE-assisted route
// ==========================================================================
//
// The GTE arm is an ALGORITHM CHANGE, not a hardware swap of the same
// algorithm: SQR gets the three squares, LZCS/LZCR gets a leading-zero seed
// for the reciprocal square root, and Newton finishes on the CPU. Accuracy is
// printed alongside so a speed win that costs precision is visible.

static inline FixedPoint<> gteInvSqrtSeed(FixedPoint<> s) {
    writeRaw<GTE::Register::LZCS, GTE::Safe>((uint32_t)s.raw());
    uint32_t lz = GTE::readRaw<GTE::Register::LZCR>();
    // s.raw() ~ 2^(31 - lz), i.e. s ~ 2^(19 - lz) in real units.
    // 1/sqrt(s) ~ 2^((lz - 19) / 2), raw = 2^(12 + (lz - 19) / 2).
    int32_t e = 12 + ((int32_t)lz - 19) / 2;
    if (e < 0) e = 0;
    if (e > 30) e = 30;
    return FixedPoint<>((int32_t)1 << e, FixedPoint<>::RAW);
}

static inline void gteNormalize(Vec3 *v) {
    GTE::writeUnsafe<GTE::Register::IR1>(toShort(v->x));
    GTE::writeUnsafe<GTE::Register::IR2>(toShort(v->y));
    GTE::writeSafe<GTE::Register::IR3>(toShort(v->z));
    K::sqr();
    uint32_t x2 = GTE::readRaw<GTE::Register::MAC1>();
    uint32_t y2 = GTE::readRaw<GTE::Register::MAC2, GTE::Unsafe>();
    uint32_t z2 = GTE::readRaw<GTE::Register::MAC3, GTE::Unsafe>();
    FixedPoint<> s((int32_t)(x2 + y2 + z2), FixedPoint<>::RAW);
    FixedPoint<> r = gteInvSqrtSeed(s);
    r *= (1.5_fp - (s * r * r) / 2);
    r *= (1.5_fp - (s * r * r) / 2);
    r *= (1.5_fp - (s * r * r) / 2);
    v->x *= r;
    v->y *= r;
    v->z *= r;
}

static uint32_t norm_cpu(unsigned n) {
    resetNormScratch(n);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        SoftMath::normalizeVec3(&s_vnorm[i]);
    }
    return TIME_END();
}

static uint32_t fastnorm_cpu(unsigned n) {
    resetNormScratch(n);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        SoftMath::fastNormalizeVec3(&s_vnorm[i]);
    }
    return TIME_END();
}

// No cold/hot split: this route has no per-batch setup at all, so the two are
// identical by construction. Reported once, under hot.
static uint32_t norm_gte(unsigned n) {
    resetNormScratch(n);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        gteNormalize(&s_vnorm[i]);
    }
    return TIME_END();
}

// ==========================================================================
// multiplyMatrix33 / 3x MVMVA
// ==========================================================================
//
// A*B, column j of the result = A * (column j of B). Upload A as RT, push B's
// three columns through V0/V1/V2, three MVMVAs, read three MAC triples.

static uint32_t mm_cpu(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        SoftMath::multiplyMatrix33(s_matB, s_min[i], &s_mout[i]);
    }
    return TIME_END();
}

// SoftMath::multiplyMatrix33(m1, m2, out) computes, with vs[] read as ROWS,
// out[i][j] = sum_k m2[i][k] * m1[k][j]  -  that is, out = m2 * m1, m1 on the
// right. MVMVA gives MAC_j = sum_k RT[j][k] * V[k], so matching it needs
// RT = m1 TRANSPOSED and m2's ROWS pushed through V0/V1/V2. The transpose is
// free: it is just a different index pattern in the five ctc2 packs, not a
// separate pass over the matrix.
//
// The first version of this code uploaded m1 untransposed and shuffled m2's
// columns by hand. It disagreed with the CPU on every element - caught by the
// correctness dump, not by inspection - and the hand shuffling also made the
// arm about 40% more expensive than it needed to be. A wrong GTE arm is not
// only wrong, it is slow in a way that flatters the CPU.
static inline void mmUploadRotationT(const Matrix33 &m) {
    auto pack = [](FixedPoint<> lo, FixedPoint<> hi) -> uint32_t {
        return (uint32_t)(uint16_t)(int16_t)lo.raw() | ((uint32_t)(uint16_t)(int16_t)hi.raw() << 16);
    };
    writeRaw<GTE::Register::R11R12, GTE::Unsafe>(pack(m.vs[0].x, m.vs[1].x));  // R11=m00 R12=m10
    writeRaw<GTE::Register::R13R21, GTE::Unsafe>(pack(m.vs[2].x, m.vs[0].y));  // R13=m20 R21=m01
    writeRaw<GTE::Register::R22R23, GTE::Unsafe>(pack(m.vs[1].y, m.vs[2].y));  // R22=m11 R23=m21
    writeRaw<GTE::Register::R31R32, GTE::Unsafe>(pack(m.vs[0].z, m.vs[1].z));  // R31=m02 R32=m12
    writeRaw<GTE::Register::R33, GTE::Safe>((uint32_t)(uint16_t)(int16_t)m.vs[2].z.raw());
}

static inline void mmUploadRows(const Matrix33 &b) {
    GTE::writeUnsafe<GTE::PseudoRegister::V0>(b.vs[0]);
    GTE::writeUnsafe<GTE::PseudoRegister::V1>(b.vs[1]);
    GTE::writeSafe<GTE::PseudoRegister::V2>(b.vs[2]);
}

static uint32_t mm_cold(unsigned n) {
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        mmUploadRotationT(s_matB);
        mmUploadRows(s_min[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_mout[i].vs[0]);
        K::mvmva<K::MX::RT, K::MV::V1, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_mout[i].vs[1]);
        K::mvmva<K::MX::RT, K::MV::V2, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_mout[i].vs[2]);
    }
    return TIME_END();
}

static uint32_t mm_hot(unsigned n) {
    mmUploadRotationT(s_matB);
    TIME_START();
    for (unsigned i = 0; i < n; i++) {
        mmUploadRows(s_min[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_mout[i].vs[0]);
        K::mvmva<K::MX::RT, K::MV::V1, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_mout[i].vs[1]);
        K::mvmva<K::MX::RT, K::MV::V2, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(s_mout[i].vs[2]);
    }
    return TIME_END();
}

// ==========================================================================
// Reporting
// ==========================================================================

static const unsigned s_ns[] = {1, 2, 4, 8, 16, 64, 256};
static constexpr unsigned N_SWEEP = sizeof(s_ns) / sizeof(s_ns[0]);

// Print a fixed-point cycles/element figure with two decimals, or a marker.
static void printPerEl(uint32_t total, unsigned n) {
    if (total == NO_RESULT) {
        ramsyscall_printf("   wrap ");
        return;
    }
    uint32_t hundredths = (total * 100u + n / 2u) / n;
    ramsyscall_printf(" %4u.%02u", hundredths / 100u, hundredths % 100u);
}

struct Arm {
    const char *name;
    bench_fn fn;
};

// Derive the setup cost and the CPU crossover from the per-element figures.
// cold and hot differ by exactly one setup per element, so S = cold - hot, and
// a batched API breaks even against the CPU at N* = S / (cpu - hot). If cold
// already beats cpu there is no crossover at all: the stateless free function
// wins at N=1 and batching only widens the margin.
static void analyseCrossover(uint32_t cpu_h, uint32_t cold_h, uint32_t hot_h) {
    ramsyscall_printf("    setup cost S = cold - hot = %u.%02u cyc\n", (cold_h - hot_h) / 100u,
                      (cold_h - hot_h) % 100u);
    if (cpu_h <= hot_h) {
        ramsyscall_printf("    CROSSOVER: none - the CPU beats even the hot GTE path at every N.\n");
        return;
    }
    if (cold_h < cpu_h) {
        ramsyscall_printf("    CROSSOVER: NONE NEEDED - cold GTE (%u.%02u) already beats the CPU\n",
                          cold_h / 100u, cold_h % 100u);
        ramsyscall_printf("               (%u.%02u) at N=1. A stateless free function wins outright.\n",
                          cpu_h / 100u, cpu_h % 100u);
        return;
    }
    uint32_t denom = cpu_h - hot_h;
    uint32_t s = cold_h - hot_h;
    uint32_t nstar = (s + denom - 1u) / denom;
    ramsyscall_printf("    CROSSOVER: hot GTE beats the CPU from N >= %u; cold GTE never does.\n", nstar);
}

static void runOp(const char *opName, const Arm *arms, unsigned nArms, unsigned nCap) {
    static uint32_t results[8][N_SWEEP];

    ramsyscall_printf("\n=== %s ===\n", opName);
    ramsyscall_printf("  raw cycles for the whole batch (min of 8 runs)\n");
    ramsyscall_printf("      N ");
    for (unsigned a = 0; a < nArms; a++) ramsyscall_printf("%9s", arms[a].name);
    ramsyscall_printf("\n");

    static bool dead[8];
    static uint32_t prev[8];
    static unsigned prevN[8];
    for (unsigned a = 0; a < nArms; a++) {
        dead[a] = false;
        prev[a] = 0;
        prevN[a] = 0;
    }

    for (unsigned k = 0; k < N_SWEEP; k++) {
        unsigned n = s_ns[k];
        ramsyscall_printf("  %5u ", n);
        for (unsigned a = 0; a < nArms; a++) {
            uint32_t r;
            if (n > nCap || dead[a]) {
                r = NO_RESULT;
            } else {
                r = bench(arms[a].fn, n);
                if (r != NO_RESULT && prevN[a] != 0) {
                    // Linear extrapolation from the previous N. Anything below
                    // 3/4 of it wrapped the 16-bit counter.
                    uint32_t expect = prev[a] * (n / prevN[a]);
                    if (r < expect - expect / 4u) r = NO_RESULT;
                }
                if (r == NO_RESULT) {
                    dead[a] = true;
                } else {
                    prev[a] = r;
                    prevN[a] = n;
                }
            }
            results[a][k] = r;
            if (r == NO_RESULT) {
                ramsyscall_printf("     wrap");
            } else {
                ramsyscall_printf("%9u", r);
            }
        }
        ramsyscall_printf("\n");
    }

    ramsyscall_printf("  cycles per element\n");
    ramsyscall_printf("      N ");
    for (unsigned a = 0; a < nArms; a++) ramsyscall_printf("%9s", arms[a].name);
    ramsyscall_printf("\n");
    for (unsigned k = 0; k < N_SWEEP; k++) {
        ramsyscall_printf("  %5u ", s_ns[k]);
        for (unsigned a = 0; a < nArms; a++) {
            printPerEl(results[a][k], s_ns[k]);
        }
        ramsyscall_printf("\n");
    }

    // Largest N at which every arm produced a number: the best per-element
    // estimate, and the one the crossover is derived from.
    int best = -1;
    for (int k = (int)N_SWEEP - 1; k >= 0; k--) {
        bool all = true;
        for (unsigned a = 0; a < nArms; a++) {
            if (results[a][k] == NO_RESULT) all = false;
        }
        if (all) {
            best = k;
            break;
        }
    }
    if (best < 0) {
        ramsyscall_printf("  ANALYSIS: no N where every arm fit under the counter wrap.\n");
        return;
    }
    ramsyscall_printf("  ANALYSIS (from N=%u, all arms live):\n", s_ns[best]);
    static uint32_t perEl[8];
    for (unsigned a = 0; a < nArms; a++) {
        perEl[a] = (results[a][best] * 100u + s_ns[best] / 2u) / s_ns[best];
        ramsyscall_printf("    %-9s %u.%02u cyc/el\n", arms[a].name, perEl[a] / 100u, perEl[a] % 100u);
    }
    int iCpu = -1, iCold = -1, iHot = -1;
    for (unsigned a = 0; a < nArms; a++) {
        const char *n = arms[a].name;
        if (n[0] == 'c' && n[1] == 'p' && n[2] == 'u') iCpu = (int)a;
        if (n[0] == 'c' && n[1] == 'o' && n[2] == 'l') iCold = (int)a;
        if (n[0] == 'h' && n[1] == 'o' && n[2] == 't' && n[3] == 0) iHot = (int)a;
    }
    if (iCpu >= 0 && iCold >= 0 && iHot >= 0) {
        analyseCrossover(perEl[iCpu], perEl[iCold], perEl[iHot]);
    }
}

// ==========================================================================
// Correctness dump - runs on the emulator too, and is what says whether the
// GTE arms compute the right thing before any hardware number is believed.
// ==========================================================================

static void checkFlag(const char *what) {
    uint32_t flag = GTE::readRaw<GTE::Register::FLAG>();
    ramsyscall_printf("    %s FLAG=0x%08x %s\n", what, flag, flag ? "*** NONZERO ***" : "clean");
}

static void correctness() {
    ramsyscall_printf("\n=== correctness / equivalence dump ===\n");

    // --- matrixVecMul3 ---
    ramsyscall_printf("  matrixVecMul3 (raw 12.12):\n");
    for (unsigned i = 0; i < 4; i++) {
        Vec3 c;
        SoftMath::matrixVecMul3(s_mat, s_vin[i], &c);
        writeRaw<GTE::Register::FLAG, GTE::Safe>(0u);
        GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        Vec3 g;
        GTE::read<GTE::PseudoRegister::LV>(g);
        ramsyscall_printf("    in=(%d,%d,%d) cpu=(%d,%d,%d) gte=(%d,%d,%d) d=(%d,%d,%d)\n", s_vin[i].x.raw(),
                          s_vin[i].y.raw(), s_vin[i].z.raw(), c.x.raw(), c.y.raw(), c.z.raw(), g.x.raw(), g.y.raw(),
                          g.z.raw(), g.x.raw() - c.x.raw(), g.y.raw() - c.y.raw(), g.z.raw() - c.z.raw());
    }
    checkFlag("matrixVecMul3");

    // Is Unsafe actually correct for the V0 write immediately before MVMVA?
    ramsyscall_printf("  Unsafe-vs-Safe V0 write before MVMVA:\n");
    {
        unsigned mismatches = 0;
        for (unsigned i = 0; i < 64; i++) {
            Vec3 a, b;
            GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
            GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
            K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
            GTE::read<GTE::PseudoRegister::LV>(a);
            GTE::writeUnsafe<GTE::PseudoRegister::Rotation>(s_mat);
            GTE::writeUnsafe<GTE::PseudoRegister::V0>(s_vin[i]);
            K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
            GTE::read<GTE::PseudoRegister::LV>(b);
            if (a.x.raw() != b.x.raw() || a.y.raw() != b.y.raw() || a.z.raw() != b.z.raw()) mismatches++;
        }
        ramsyscall_printf("    %u/64 mismatches -> Unsafe is %s here\n", mismatches,
                          mismatches ? "*** WRONG ***" : "correct");
    }

    // --- crossProductVec3 ---
    ramsyscall_printf("  crossProductVec3 (raw 12.12):\n");
    for (unsigned i = 0; i < 3; i++) {
        Vec3 c;
        SoftMath::crossProductVec3(s_mat.vs[0], s_vin[i], &c);
        writeRaw<GTE::Register::FLAG, GTE::Safe>(0u);
        cpWriteDiag(s_mat.vs[0]);
        cpWriteIr(s_vin[i]);
        K::cp();
        Vec3 g;
        GTE::read<GTE::PseudoRegister::LV>(g);
        ramsyscall_printf("    cpu=(%d,%d,%d) gte=(%d,%d,%d) d=(%d,%d,%d)\n", c.x.raw(), c.y.raw(), c.z.raw(),
                          g.x.raw(), g.y.raw(), g.z.raw(), g.x.raw() - c.x.raw(), g.y.raw() - c.y.raw(),
                          g.z.raw() - c.z.raw());
    }
    checkFlag("crossProductVec3");

    // --- project / RTPS ---
    ramsyscall_printf("  project vs RTPS (NOT numerically equivalent by design):\n");
    for (unsigned i = 0; i < 3; i++) {
        Vec2 c;
        SoftMath::project(&s_vin[i], PROJ_H, &c);
        writeRaw<GTE::Register::FLAG, GTE::Safe>(0u);
        uploadRtpsConfig();
        GTE::writeSafe<GTE::PseudoRegister::V0>(s_vin[i]);
        K::rtps();
        uint32_t sxy2 = GTE::readRaw<GTE::Register::SXY2>();
        uint32_t sz3 = GTE::readRaw<GTE::Register::SZ3, GTE::Unsafe>();
        ramsyscall_printf("    cpu=(%d,%d) rtps sx=%d sy=%d sz3=%u\n", c.x.raw(), c.y.raw(), (int16_t)(sxy2 & 0xffff),
                          (int16_t)(sxy2 >> 16), sz3);
    }
    checkFlag("RTPS");

    // --- normalize ---
    ramsyscall_printf("  normalize (raw 12.12, 4096 = 1.0; norm should be 4096):\n");
    for (unsigned i = 0; i < 4; i++) {
        Vec3 a = s_vin[i], b = s_vin[i], c = s_vin[i];
        SoftMath::normalizeVec3(&a);
        SoftMath::fastNormalizeVec3(&b);
        writeRaw<GTE::Register::FLAG, GTE::Safe>(0u);
        gteNormalize(&c);
        ramsyscall_printf("    |exact|=%d |fast|=%d |gte|=%d\n", SoftMath::normOfVec3(a).raw(),
                          SoftMath::normOfVec3(b).raw(), SoftMath::normOfVec3(c).raw());
    }
    checkFlag("normalize");

    // --- multiplyMatrix33 ---
    ramsyscall_printf("  multiplyMatrix33 (row 0, raw 12.12):\n");
    for (unsigned i = 0; i < 2; i++) {
        Matrix33 c;
        SoftMath::multiplyMatrix33(s_matB, s_min[i], &c);
        writeRaw<GTE::Register::FLAG, GTE::Safe>(0u);
        mmUploadRotationT(s_matB);
        mmUploadRows(s_min[i]);
        Matrix33 g;
        K::mvmva<K::MX::RT, K::MV::V0, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(g.vs[0]);
        K::mvmva<K::MX::RT, K::MV::V1, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(g.vs[1]);
        K::mvmva<K::MX::RT, K::MV::V2, K::TV::Zero, K::SF::Shifted, K::LM::Unlimited>();
        GTE::read<GTE::PseudoRegister::LV>(g.vs[2]);
        ramsyscall_printf("    cpu=(%d,%d,%d) gte=(%d,%d,%d)\n", c.vs[0].x.raw(), c.vs[0].y.raw(), c.vs[0].z.raw(),
                          g.vs[0].x.raw(), g.vs[0].y.raw(), g.vs[0].z.raw());
    }
    checkFlag("multiplyMatrix33");
}

// ==========================================================================
// Instrument check
// ==========================================================================
//
// A block of exactly 1024 nops must cost 1024 cycles plus the bracket. If it
// does not, the counter is not in system-clock mode and no other number in
// this run means anything.

#define REP4(x) x x x x
#define REP16(x) REP4(x) REP4(x) REP4(x) REP4(x)
#define REP256(x) REP16(x) REP16(x) REP16(x) REP16(x) REP16(x) REP16(x) REP16(x) REP16(x) \
                  REP16(x) REP16(x) REP16(x) REP16(x) REP16(x) REP16(x) REP16(x) REP16(x)

static bool instrumentCheck() {
    // 256 nops = 1 KiB, comfortably inside the 4 KiB icache. 1024 nops is
    // exactly the whole icache and thrashes it: measured 2051 cycles for 1024
    // nops, i.e. ~2 cyc/instruction of pure fetch stall. That is a real
    // observation about the icache, not a broken counter, but it makes a
    // useless instrument check.
    uint32_t best = 0xffffu;
    for (int i = 0; i < 8; i++) {
        TIME_START();
        __asm__ volatile(REP256("nop\n") : : : "memory");
        uint32_t d = TIME_END();
        if (d < best) best = d;
    }
    uint32_t empty = 0xffffu;
    for (int i = 0; i < 8; i++) {
        TIME_START();
        __asm__ volatile("" : : : "memory");
        uint32_t d = TIME_END();
        if (d < empty) empty = d;
    }
    ramsyscall_printf("=== instrument check ===\n");
    ramsyscall_printf("  256 nops: %u cycles (expect ~256 + bracket)\n", best);
    ramsyscall_printf("  empty bracket: %u cycles\n", empty);
    bool ok = (best >= 256) && (best <= 256 + 32);
    ramsyscall_printf("  counter-2 system-clock mode: %s\n",
                      ok ? "OK" : "*** OFF EXPECTED - treat every timing below as SUSPECT ***");
    return ok;
}

// ==========================================================================

static const Arm ARMS_MV[] = {
    {"ctl", mv_ctl}, {"cpu", mv_cpu}, {"cold", mv_cold}, {"hot", mv_hot}, {"hotU", mv_hotU},
};
static const Arm ARMS_MVXY[] = {
    {"cpu", mvxy_cpu}, {"cold", mvxy_cold}, {"hot", mvxy_hot},
};
static const Arm ARMS_CP[] = {
    {"cpu", cp_cpu}, {"cold", cp_cold}, {"hot", cp_hot},
};
static const Arm ARMS_PROJ[] = {
    {"ctl", proj_ctl}, {"cpu", proj_cpu}, {"cold", proj_cold}, {"hot", proj_hot}, {"hotU", proj_hotU},
};
static const Arm ARMS_NORM[] = {
    {"exact", norm_cpu}, {"fast", fastnorm_cpu}, {"gte", norm_gte},
};
static const Arm ARMS_MM[] = {
    {"cpu", mm_cpu}, {"cold", mm_cold}, {"hot", mm_hot},
};

int main() {
    gteEnable();
    initData();

    ramsyscall_printf("*** GTE vs SoftMath throughput benchmark ***\n");

#if PCSX_TESTS
    ramsyscall_printf("PCSX_TESTS: emulator run, timings SKIPPED (no access-cost model).\n");
    ramsyscall_printf("Correctness dump only.\n");
    correctness();
    for (unsigned i = 0; i < N_MAX; i++) s_sink += s_vout[i].x.raw() + s_v2out[i].x.raw() + s_vnorm[i].x.raw();
    pcsx_exit(0);
    return 0;
#else
    int wasEnabled = enterCriticalSection();
    // Root counter 2, system-clock source (mode bits 8-9 = 00), free running.
    // Writing the mode resets the value to 0.
    COUNTERS[2].mode = 0;

    bool ok = instrumentCheck();

    correctness();

    // Deliberately NOT gated on `ok`: a failed instrument check makes the
    // numbers suspect, not absent, and a run that prints nothing cannot be
    // diagnosed. The banner above says which it is.
    {
        runOp("matrixVecMul3 (MVMVA)", ARMS_MV, 5, N_MAX);
        runOp("matrixVecMul3xy (MVMVA)", ARMS_MVXY, 3, N_MAX);
        runOp("crossProductVec3 (CP)", ARMS_CP, 3, N_MAX);
        runOp("project (RTPS)", ARMS_PROJ, 5, N_MAX);
        runOp("normalizeVec3 / fastNormalizeVec3 (SQR+LZCS+Newton)", ARMS_NORM, 3, N_MAX);
        runOp("multiplyMatrix33 (3x MVMVA)", ARMS_MM, 3, N_MAT);
    }
    if (!ok) ramsyscall_printf("\n*** instrument check FAILED - timings above are suspect ***\n");

    if (wasEnabled) leaveCriticalSection();

    // Consume every output so nothing above can be dead-code eliminated.
    for (unsigned i = 0; i < N_MAX; i++) s_sink += s_vout[i].x.raw() + s_v2out[i].x.raw() + s_vnorm[i].x.raw();
    for (unsigned i = 0; i < N_MAT; i++) s_sink += s_mout[i].vs[0].x.raw();
    ramsyscall_printf("\nsink=0x%08x\n", s_sink);
    ramsyscall_printf("*** benchmark complete ***\n");
    // pcsx_exit is a write to an inert MMIO address on real silicon, so on
    // hardware this returns and `return 0` would fall back into the BIOS - the
    // first hardware run did exactly that and re-ran the tail of main, printing
    // a second, doubled sink. Park here instead so the serial log ends once.
    pcsx_exit(0);
    while (1) __asm__ volatile("");
#endif
}
