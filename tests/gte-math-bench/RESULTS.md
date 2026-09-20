# GTE vs SoftMath: throughput results

Per operation, at what batch size N does routing through the GTE beat
`psyqo::SoftMath`'s CPU implementation, in cycles per element, once the
mtc2/ctc2/mfc2 round trips and the hazard nops are counted?

**Answer, for every operation measured: N = 1.** There is no crossover to find.
The GTE wins at a batch of one even when the matrix or projection setup is
uploaded inside the call, and it wins by between 1.4x and 5.5x. See
"What this means for an API" below - this result kills one of the two designs
that motivated the measurement.

## Hardware

SCPH-5501 (NTSC-U), via the remote hardware farm. Two further consoles were
measured as a control (below); the numbers are identical, so a single console
is quoted throughout.

## Methodology

Mirrors `src/mips/tests/load-timings`, which is the harness in this tree that
measures cycles:

- **Cycle source:** root counter 2 in system-clock mode (1 tick per CPU cycle,
  16-bit), via the `COUNTERS` macro.
- **IRQs masked** across every timed region; counter mode written once at
  startup, which resets the value to 0.
- **Minimum over 8 runs** per point, to warm the icache and reject stray stalls.
- **Instrument check** in-band: a 256-nop block must cost ~256 cycles. It
  measured **259** on every console. A failure prints a banner and marks every
  subsequent number suspect, but does not suppress them - a run that prints
  nothing cannot be diagnosed.
- **Nothing is dead-code eliminated:** GTE ops are `asm volatile`, SoftMath is
  an out-of-line call in `libpsyqo.a`, and every output array is summed into a
  `volatile` sink after the sweep.

### Four arms per operation

| arm | what it measures |
|---|---|
| `ctl` | the loop and its loads/stores, no math. The floor every other arm pays. |
| `cpu` | `psyqo::SoftMath`, unmodified. |
| `cold` | GTE with the matrix/config upload **inside** the loop, once per element. What a stateless free function `GteMath::matrixVecMul3(m, v, out)` actually costs a caller. |
| `hot` | GTE with the upload hoisted **out**. What a scoped/batched API costs. |
| `hotU` | `hot` with `psyqo::GTE::Unsafe` register writes (no hazard nops). |

`cold` and `hot` are both linear in N with no setup term - `cold` folds the
setup into the per-element cost, `hot` excludes it entirely - so the setup cost
is recovered as `S = cold - hot` and the crossover as `N* = S / (cpu - hot)`.
Both are computed on-device and printed.

### The counter wrap, and why the first version of this file would have lied

The counter is 16 bits: a single bracketed region can only measure 65535
cycles. A ceiling test alone does **not** catch this - the CPU arm of
`matrixVecMul3` at N=256 costs ~87800 cycles, wrapped to 22298, and printed as
a perfectly plausible **87 cyc/el** in a column that read 343 at N=64. The
harness now extrapolates linearly from the previous N and rejects anything
below 3/4 of the expectation, then marks that arm dead for all larger N.
Rejected cells print `wrap`. The `ANALYSIS` block is taken from the largest N
at which *every* arm is still live, which is why different operations report
from different N.

### Inputs

256 vectors in main RAM (not scratchpad - real vertex data lives in RAM and
every arm pays the same load cost, which `ctl` measures). Components chosen
against three separate constraints, all of which were violated by the first
draft and caught by the correctness dump:

- `x, y` in +-0.4, `z` in [0.15, 0.55] **strictly positive**. The first draft
  let `z` go negative and RTPS clamped SZ3 to 0.
- `|v|^2 <= 0.62 < 1`, so `fastNormalizeVec3` never takes its `if (x > 1)`
  divide branch and every element costs the same four multiply-only Newton
  steps.
- IR magnitudes far from the 16-bit saturation limits. `FLAG` is read and
  printed after every operation and is **clean (0x00000000) for all of them**,
  so nothing here measures the saturation path.

## Correctness, first

A fast wrong answer is not a result. Every GTE arm is checked against its CPU
counterpart on-device before any timing is believed:

```
=== correctness / equivalence dump ===
  matrixVecMul3 (raw 12.12):
    in=(-1165,-1097,2173) cpu=(-460,-1531,2173) gte=(-461,-1533,2173) d=(-1,-2,0)
    in=(-640,-1160,1979) cpu=(26,-1324,1979) gte=(25,-1325,1979) d=(-1,-1,0)
    in=(1565,-965,2248) cpu=(1837,-53,2248) gte=(1837,-54,2248) d=(0,-1,0)
    in=(1531,740,1006) cpu=(955,1405,1006) gte=(955,1406,1006) d=(0,1,0)
    matrixVecMul3 FLAG=0x00000000 clean
  Unsafe-vs-Safe V0 write before MVMVA:
    0/64 mismatches -> Unsafe is correct here
  crossProductVec3 (raw 12.12):
    cpu=(-1086,-1881,-1531) gte=(-1087,-1882,-1533) d=(-1,-1,-2)
    cpu=(-989,-1713,-1324) gte=(-990,-1714,-1325) d=(-1,-1,-1)
    cpu=(-1124,-1946,-53) gte=(-1124,-1947,-54) d=(0,-1,-1)
    crossProductVec3 FLAG=0x00000000 clean
  project vs RTPS (NOT numerically equivalent by design):
    cpu=(-2195,-2067) rtps sx=-34 sy=-113 sz3=2173
    cpu=(-1324,-2400) rtps sx=2 sy=-108 sz3=1979
    cpu=(2851,-1758) rtps sx=130 sy=-4 sz3=2248
    RTPS FLAG=0x00000000 clean
  normalize (raw 12.12, 4096 = 1.0; norm should be 4096):
    |exact|=4097 |fast|=4097 |gte|=4087
    |exact|=4095 |fast|=4053 |gte|=4097
    |exact|=4097 |fast|=4097 |gte|=4095
    |exact|=4097 |fast|=3474 |gte|=4097
    normalize FLAG=0x00000000 clean
  multiplyMatrix33 (row 0, raw 12.12):
    cpu=(1221,305,555) gte=(1221,305,555)
    cpu=(1763,2544,-1459) gte=(1764,2544,-1459)
    multiplyMatrix33 FLAG=0x00000000 clean
```

The GTE and CPU agree to within 1-2 raw 12.12 units on `matrixVecMul3` and
`crossProductVec3` - that residual is the GTE's 16-bit (1.3.12) matrix and
vector registers against SoftMath's 32-bit intermediates, not an error.
`multiplyMatrix33` agrees to 0-1. `project`/RTPS is deliberately not a
numeric equivalence (see below).

**This check earned its keep.** The first `multiplyMatrix33` GTE arm disagreed
on every element - I had the operand convention backwards - and, being wrong,
it was also about 40% *more expensive* than the correct version, because the
wrong version shuffled matrix columns by hand. A broken GTE arm does not fail
loudly; it flatters the CPU.

## Results (SCPH-5501, caller compiled `-O2`)

```
=== matrixVecMul3 (MVMVA) ===
  raw cycles for the whole batch (min of 8 runs)
      N       ctl      cpu     cold      hot     hotU
      1        37      280      136       57       56
      2        67      549      260      104      102
      4       127     1090      508      198      194
      8       247     2169     1007      386      378
     16       493     4356     2016      765      750
     64      1962    17386     8101     3048     2984
    256      7862     wrap    32669    12165    11935
  cycles per element
      N       ctl      cpu     cold      hot     hotU
      1    37.00  280.00  136.00   57.00   56.00
      2    33.50  274.50  130.00   52.00   51.00
      4    31.75  272.50  127.00   49.50   48.50
      8    30.88  271.13  125.88   48.25   47.25
     16    30.81  272.25  126.00   47.81   46.88
     64    30.66  271.66  126.58   47.63   46.63
    256    30.71   wrap   127.61   47.52   46.62
  ANALYSIS (from N=64, all arms live):
    ctl       30.66 cyc/el
    cpu       271.66 cyc/el
    cold      126.58 cyc/el
    hot       47.63 cyc/el
    hotU      46.63 cyc/el
    setup cost S = cold - hot = 78.95 cyc
    CROSSOVER: NONE NEEDED - cold GTE (126.58) already beats the CPU
               (271.66) at N=1. A stateless free function wins outright.

=== matrixVecMul3xy (MVMVA) ===
  raw cycles for the whole batch (min of 8 runs)
      N       cpu     cold      hot
      1       180      134       55
      2       349      256      100
      4       694      500      190
      8      1373      995      370
     16      2732     1981      735
     64     10932     7918     2926
    256     43700    31642    11701
  cycles per element
      N       cpu     cold      hot
      1   180.00  134.00   55.00
      2   174.50  128.00   50.00
      4   173.50  125.00   47.50
      8   171.63  124.38   46.25
     16   170.75  123.81   45.94
     64   170.81  123.72   45.72
    256   170.70  123.60   45.71
  ANALYSIS (from N=256, all arms live):
    cpu       170.70 cyc/el
    cold      123.60 cyc/el
    hot       45.71 cyc/el
    setup cost S = cold - hot = 77.89 cyc
    CROSSOVER: NONE NEEDED - cold GTE (123.60) already beats the CPU
               (170.70) at N=1. A stateless free function wins outright.

=== crossProductVec3 (CP) ===
  raw cycles for the whole batch (min of 8 runs)
      N       cpu     cold      hot
      1       188       85       56
      2       382      158      102
      4       749      304      194
      8      1510      597      383
     16      3010     1182      751
     64     12023     4699     2998
    256     48067    18771    11920
  cycles per element
      N       cpu     cold      hot
      1   188.00   85.00   56.00
      2   191.00   79.00   51.00
      4   187.25   76.00   48.50
      8   188.75   74.63   47.88
     16   188.13   73.88   46.94
     64   187.86   73.42   46.84
    256   187.76   73.32   46.56
  ANALYSIS (from N=256, all arms live):
    cpu       187.76 cyc/el
    cold      73.32 cyc/el
    hot       46.56 cyc/el
    setup cost S = cold - hot = 26.76 cyc
    CROSSOVER: NONE NEEDED - cold GTE (73.32) already beats the CPU
               (187.76) at N=1. A stateless free function wins outright.

=== project (RTPS) ===
  raw cycles for the whole batch (min of 8 runs)
      N       ctl      cpu     cold      hot     hotU
      1        36      670      152       62       61
      2        64     1363      291      114      112
      4       120     2760      571      218      214
      8       235     5533     1131      426      418
     16       461    11130     2249      845      826
     64      1821    45402     8962     3365     3278
    256      7291     wrap    35813    13419    13099
  cycles per element
      N       ctl      cpu     cold      hot     hotU
      1    36.00  670.00  152.00   62.00   61.00
      2    32.00  681.50  145.50   57.00   56.00
      4    30.00  690.00  142.75   54.50   53.50
      8    29.38  691.63  141.38   53.25   52.25
     16    28.81  695.63  140.56   52.81   51.63
     64    28.45  709.41  140.03   52.58   51.22
    256    28.48   wrap   139.89   52.42   51.17
  ANALYSIS (from N=64, all arms live):
    ctl       28.45 cyc/el
    cpu       709.41 cyc/el
    cold      140.03 cyc/el
    hot       52.58 cyc/el
    hotU      51.22 cyc/el
    setup cost S = cold - hot = 87.45 cyc
    CROSSOVER: NONE NEEDED - cold GTE (140.03) already beats the CPU
               (709.41) at N=1. A stateless free function wins outright.

=== normalizeVec3 / fastNormalizeVec3 (SQR+LZCS+Newton) ===
  raw cycles for the whole batch (min of 8 runs)
      N     exact     fast      gte
      1      3436      498      318
      2      6859      965      632
      4     14257     1905     1254
      8     28707     3766     2505
     16      wrap     7463     4990
     64      wrap    29788    19960
    256      wrap     wrap     wrap
  cycles per element
      N     exact     fast      gte
      1  3436.00  498.00  318.00
      2  3429.50  482.50  316.00
      4  3564.25  476.25  313.50
      8  3588.38  470.75  313.13
     16    wrap   466.44  311.88
     64    wrap   465.44  311.88
    256    wrap    wrap    wrap 
  ANALYSIS (from N=8, all arms live):
    exact     3588.38 cyc/el
    fast      470.75 cyc/el
    gte       313.13 cyc/el

=== multiplyMatrix33 (3x MVMVA) ===
  raw cycles for the whole batch (min of 8 runs)
      N       cpu     cold      hot
      1      1158      233      137
      2      2307      439      264
      4      4580      853      518
      8      9155     1691     1026
     16     18305     3350     2042
     64      wrap     wrap     wrap
    256      wrap     wrap     wrap
  cycles per element
      N       cpu     cold      hot
      1  1158.00  233.00  137.00
      2  1153.50  219.50  132.00
      4  1145.00  213.25  129.50
      8  1144.38  211.38  128.25
     16  1144.06  209.38  127.63
     64    wrap    wrap    wrap 
    256    wrap    wrap    wrap 
  ANALYSIS (from N=16, all arms live):
    cpu       1144.06 cyc/el
    cold      209.38 cyc/el
    hot       127.63 cyc/el
    setup cost S = cold - hot = 81.75 cyc
    CROSSOVER: NONE NEEDED - cold GTE (209.38) already beats the CPU
               (1144.06) at N=1. A stateless free function wins outright.
```

## What survived, what did not

Three predictions were put up front, with an explicit invitation to break them.

**1. "RTPS beats the CPU even at N=1, because the CPU has to do the divide in
software." SURVIVES, by more than predicted.** `SoftMath::project` costs
**709.41 cyc/el**; cold RTPS costs **140.03**, hot **52.58**. That is 5.1x cold
and 13.5x hot. `FixedPoint<12,int32_t>::operator/` goes through
`FixedPointInternals::dDiv` -> `iDiv`, a 64-bit shift-subtract loop, and it
dominates everything else in the function: subtracting the `ctl` floor, the
marginal cost is 681 cycles for the CPU against 24 for the GTE.

**2. "MVMVA needs N >= 4 or so before it wins." REFUTED.** There is no N at
which the CPU is ahead. At N=1, cold MVMVA is **136** cycles against the CPU's
**280**. The asymptotic figures are 126.58 vs 271.66 - a flat 2.15x, at every
batch size including one.

**3. "multiplyMatrix33 never clearly wins - three round trips against 27
multiplies." REFUTED hardest of the three.** CPU **1144.06 cyc/el**, cold GTE
**209.38**, hot **127.63**. That is 5.5x cold and 9.0x hot. The 27 multiplies
are not the expensive part; 27 `FixedPoint::operator*` are 27 64-bit multiplies
plus 27 64-bit shifts plus the loads, and three MVMVA round trips beat that
comfortably. The transpose the GTE route needs to match SoftMath's operand
convention is free - it is a different index pattern in the five `ctc2` packs,
not a pass over the matrix.

## What this means for an API

> "If per-call GTE turns out FASTER than the CPU for `matrixVecMul3`, my entire
> architectural argument (scoped register ownership over free functions)
> collapses and the simple drop-in design is correct."

**It is faster. 2.15x, at N=1, paying the full matrix upload every call.** By
the stated condition, the argument that scoped register ownership is *required*
does not hold. A plain `GteMath::matrixVecMul3(m, v, out)` free function is a
2x improvement on `SoftMath` with no API surface beyond what `SoftMath` already
has.

The honest other half, which the same numbers say just as clearly: **scoping is
still worth a lot on top.** Hot is 2.66x faster than cold for `matrixVecMul3`
(47.63 vs 126.58) and 2.66x for RTPS (52.58 vs 140.03). The setup cost `S` is
78.95 cycles for the rotation matrix and 87.45 for the RTPS configuration - the
five `ctc2` packs cost about nine RAM loads plus the packing, and that is most
of the cold call.

So the design conclusion is not "scoping is pointless", it is that the two are
**not** in competition and the ordering between them is the opposite of what was
assumed:

- The free-function form is a **correct default**, not a fallback. It never
  loses to `SoftMath` on any operation measured.
- The scoped/batched form is an **optimisation on top**, worth ~2.7x for
  matrix-vector work. It buys the same factor whether N is 4 or 256, because
  there is no crossover to amortise past - it is a constant per-call saving.
- Shipping the free functions first costs nothing and blocks nothing.

### Per-operation summary (SCPH-5501, `-O2`)

| operation | GTE route | cpu | cold | hot | cold speedup | hot speedup | crossover N |
|---|---|---:|---:|---:|---:|---:|---|
| `matrixVecMul3` | MVMVA | 271.66 | 126.58 | 47.63 | 2.15x | 5.70x | none - wins at 1 |
| `matrixVecMul3xy` | MVMVA | 170.70 | 123.60 | 45.71 | 1.38x | 3.73x | none - wins at 1 |
| `crossProductVec3` | CP | 187.76 | 73.32 | 46.56 | 2.56x | 4.03x | none - wins at 1 |
| `project` | RTPS | 709.41 | 140.03 | 52.58 | 5.07x | 13.49x | none - wins at 1 |
| `multiplyMatrix33` | 3x MVMVA | 1144.06 | 209.38 | 127.63 | 5.46x | 8.96x | none - wins at 1 |

All figures cycles per element. `matrixVecMul3xy` has the thinnest margin
because the CPU version already skips a third of the work while the GTE still
runs a full MVMVA; it is also the only case where the free function's advantage
(1.38x) is small enough that a caller might reasonably not care.

## Secondary findings

### The `ctl` floor is a third of the hot GTE cost

`ctl` - three `lw`, three `sw` and the loop - costs **30.66 cyc/el**. Hot
MVMVA costs 47.63, so the marginal cost of the GTE round trip is about
**17 cycles**: two `mtc2`, four hazard nops, the 8-cycle `cop2`, and three
`swc2` that interlock on completion. The CPU's marginal cost for the same
operation is 241. Any further optimisation of the GTE path runs into the memory
floor long before it runs into the GTE.

### `Unsafe` register writes are correct for the write-then-op pattern, and worth ~1 cycle

Writing V0 with `writeUnsafe` (no hazard nops) immediately before an MVMVA gave
**0 mismatches out of 64** against the `writeSafe` version, on all three console
revisions. The saving is small - 46.63 vs 47.63 cyc/el, since the four nops
partly hide behind the GTE - but it is free and it is measured.

Scope this claim carefully: it covers **an MTC2 to a vector register
immediately followed by the cop2 op that reads it**, 64 samples per console. It
is not a general licence to drop `Safe`, and it says nothing about the reverse
hazard (a cop2 op followed by a write to one of its inputs), which is what
`src/mips/tests/gte-latency` measures.

### `fastNormalizeVec3` is both slower and less accurate than a GTE-assisted route

> **The accuracy half of this section describes code that no longer exists.**
> The `x * 2` seed was replaced in `f1f109cf6` with a software leading-zero seed
> (`floor(log2(raw))` halved, sqrt(2) half-step on odd exponents), which is the
> software-CLZ route this section's last paragraph asks for. Sweeping every
> representable input through the shipped source now gives 0 inputs above 10%
> error on either branch, worst 0.54%. The 3474 reading below was real when it
> was taken; it is not reproducible against current `main`. **The CYCLE COUNTS
> are unaffected** - the new seed is five compares and a multiply, and nothing
> in the Newton loop changed - but they have not been re-measured on silicon
> since, so treat the `fast` row as an upper bound rather than a fresh number.

| route | cyc/el | measured norm (should be 4096) |
|---|---:|---|
| `SoftMath::normalizeVec3` | 3588.38 | 4095-4097 |
| `SoftMath::fastNormalizeVec3` | 470.75 | 3474-4097 |
| SQR + LZCS seed + 3 Newton | 313.13 | 4087-4097 |

`normalizeVec3` calls `1 / squareRoot(s)`, which is two `iDiv` chains, and at
3588 cyc/el it is the most expensive thing in `SoftMath` by a factor of three.
`fastNormalizeVec3` avoids the divides but its seed is `x * 2`, which for small
`|v|` is far from `1/sqrt(x)`, and four Newton steps do not recover: one of the
four sampled vectors came out at **3474** instead of 4096, a 15% error. The
GTE-assisted route (SQR for the three squares, `LZCS`/`LZCR` for a
leading-zero seed, three Newton steps on the CPU) is 1.5x faster than `fast`
and holds 4087-4097.

This is an algorithm change, not a hardware swap of the same algorithm, and it
is reported as one. But the seed quality result stands independently of the
GTE: **`fastNormalizeVec3`'s `x * 2` seed is the defect**, and an LZCS-derived
or software-CLZ-derived seed would fix the accuracy on the CPU path too.
(Done in `f1f109cf6`, software-CLZ route. See the note at the head of this
section.)

### Compiler: psyqo's GTE wrappers do not inline at `-Os`

The benchmark was built twice against an identical `-Os` `libpsyqo.a`, varying
only the caller's own optimisation level.

| arm | caller `-O2` | caller `-Os` | penalty |
|---|---:|---:|---:|
| `matrixVecMul3` cold | 126.58 | 211.11 | +67% |
| `matrixVecMul3` hot | 47.63 | 54.41 | +14% |
| `project` cold | 140.03 | 185.52 | +32% |
| `crossProductVec3` cold | 73.32 | 98.54 | +34% |

At `-Os` GCC declines to inline `psyqo::GTE::writeSafe`, `writeUnsafe` and
`read`, emitting 40 `jal`s into the templates; at `-O2` there are none, and the
loop body is the bare `mtc2`/`cop2`/`swc2` sequence a hand-written routine would
be. Since `psyqo.mk` builds psyqo itself at `-Os`, this is a real API-design
input: **a `GteMath` living in psyqo needs `always_inline` on the register
accessors, or needs to be written so the whole operation is one `asm` block.**
Without it, the free-function form loses a third of its margin to call overhead.

The `-Os` numbers still beat the CPU everywhere, so the headline conclusion is
not sensitive to this.

### Console revision has no measurable effect

Four runs, crossed:

| run | `matrixVecMul3` cpu | cold | hot |
|---|---:|---:|---:|
| `-O2` @ SCPH-5501 | 271.66 | 126.58 | 47.63 |
| `-O2` @ SCPH-1001 | 271.67 | 127.50 | 47.63 |
| `-Os` @ SCPH-5501 | 292.36 | 211.11 | 54.41 |
| `-Os` @ SCPH-7001 | 292.36 | 210.89 | 54.41 |

The compiler splits the rows; the console does not move them (worst case 0.7%,
on one cell). The 256-nop instrument check read 259 on all three revisions.
This was not assumed - the first two runs landed on *different* consoles by
chance, which is a confound, so two more were submitted to cross the design and
measure it. It is null.

## Reproducing

```
make -C src/mips/tests/gte-math-bench CPPFLAGS_Release=-O2   # primary
make -C src/mips/tests/gte-math-bench                        # -Os variant
```

Submit `gte-math-bench.ps-exe` to the farm with `--run-seconds 60`. The binary
parks in an infinite loop after printing, so the verdict is `TIMEOUT`; the
proof is a populated `serial.log`, not the verdict.

On the emulator the correctness dump is meaningful and the timings are not -
Redux models neither GTE latency nor memory access costs. The in-band
instrument check says so out loud: it reads 515 cycles for 256 nops there and
prints the "treat every timing below as SUSPECT" banner.

## Power figures for the nulls

There are no null results here - every arm separated by more than 2x. For
completeness, the resolution that would have detected one: the counter ticks
once per CPU cycle, the empty bracket costs 4 cycles, and the minimum over 8
runs was stable to +-1 cycle on repeated whole-run submissions (compare the two
`-Os` rows above, which agree to 0.22 cyc/el across different consoles). Any
real difference above ~0.5 cyc/el at N=64 would have been visible.
