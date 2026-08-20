# GTE vs SoftMath throughput benchmark

Measures, on real silicon, the cycles per element of routing a math operation
through the GTE against `psyqo::SoftMath`'s CPU implementation - counting the
mtc2/ctc2/mfc2 round trips and the hazard nops, not just the cop2 op.

Distinct from `src/mips/tests/gte-latency`, which measures hazard *boundaries*
(how many instruction slots must separate a GTE op from the next write to one
of its inputs). That answers a different question and uses no timer at all.
The timing methodology here is copied from `src/mips/tests/load-timings`.

Hardware results and the API conclusions they support: [RESULTS.md](RESULTS.md).

## Operations covered

| SoftMath | GTE route |
|---|---|
| `project` | `RTPS` |
| `matrixVecMul3`, `matrixVecMul3xy` | `MVMVA` |
| `crossProductVec3` | `CP` (Sony's "OP") |
| `normalizeVec3` / `fastNormalizeVec3` | `SQR` + `LZCS`/`LZCR` seed + Newton |
| `multiplyMatrix33` | 3x `MVMVA`, B's rows through V0/V1/V2 against A transposed |

## Arms

Each operation is swept over N = 1, 2, 4, 8, 16, 64, 256 with up to five arms:

- `ctl` - the loop and its loads/stores, no math. The floor every other arm
  pays; subtracting it gives the marginal cost of the math.
- `cpu` - `psyqo::SoftMath`, unmodified.
- `cold` - GTE with the matrix/config upload **inside** the loop, once per
  element. What a stateless free function costs a caller.
- `hot` - GTE with the upload hoisted **out**. What a scoped/batched API costs.
- `hotU` - `hot` with `psyqo::GTE::Unsafe` writes, to price the hazard nops.

`cold` and `hot` are both linear in N with no setup term, so the setup cost is
`S = cold - hot` and the CPU crossover is `N* = S / (cpu - hot)`. Both are
computed on-device and printed in the `ANALYSIS` block.

## Timing

Root counter 2 in system-clock mode (1 tick per CPU cycle, 16-bit) via the
`COUNTERS` macro; IRQs masked across every timed region; minimum over 8 runs
per point to warm the icache and reject stray stalls.

Two guards that are load-bearing rather than decorative:

- **Instrument check.** A 256-nop block must cost ~256 cycles (measures 259 on
  hardware). A failure prints a banner marking every subsequent number suspect
  but does *not* suppress the sweep - a run that prints nothing cannot be
  diagnosed. 1024 nops is exactly the 4 KiB icache and thrashes it: that block
  measures ~2051 cycles, i.e. ~2 cyc/instruction of pure fetch stall, which is
  a real observation about the icache and a useless instrument check.
- **Counter-wrap rejection.** The counter is 16-bit, so one bracketed region
  tops out at 65535 cycles. A ceiling test alone does not catch a wrap: the CPU
  arm of `matrixVecMul3` at N=256 wrapped 87800 cycles down to 22298 and
  printed as a plausible 87 cyc/el. Each point is checked against a linear
  extrapolation from the previous N and rejected below 3/4 of it; the arm is
  then marked dead for all larger N and prints `wrap`.

## Correctness dump

Runs before the sweep and is meaningful on the emulator too. Every GTE arm is
compared against its CPU counterpart element by element, and the GTE `FLAG`
register is read and printed after each operation to confirm nothing is
measuring the saturation path.

This is not ceremony. The first version of the `multiplyMatrix33` GTE arm had
the operand convention backwards; it disagreed on every element and was also
~40% more expensive than the correct version. A broken GTE arm does not fail
loudly, it flatters the CPU.

Note the GTE is enabled by hand (`CU2` in `CP0.SR`) because this program does
not use `psyqo::Application` - `Application::run()` never returns and blocks on
VBlank. Without that, every cop2 instruction is a silent no-op and every GTE
arm reads zeros, which is exactly what the first emulator run showed.

## Build

```
make -C src/mips/tests/gte-math-bench CPPFLAGS_Release=-O2   # primary
make -C src/mips/tests/gte-math-bench                        # -Os variant
```

`libpsyqo.a` is built at `-Os` either way (`psyqo.mk` hardcodes it), so the two
differ only in whether the caller inlines psyqo's GTE register accessors. At
`-Os` GCC declines and emits 40 `jal`s into the templates; that costs up to 67%
on the `cold` arms. See RESULTS.md.

## Running

Upload the `.ps-exe` to hardware and capture serial. The binary parks in an
infinite loop after printing, so a farm run ends `TIMEOUT`; the proof is a
populated `serial.log`, not the verdict.

On the emulator the timings are meaningless - Redux models neither GTE latency
nor memory access costs - and the instrument check says so in-band. Use the
emulator to validate the correctness dump, hardware for the numbers.

## Not registered in `src/mips/tests/Makefile`

Deliberate. This is a benchmark, not a test: it has no pass/fail criterion, its
numbers are only meaningful on hardware, and it parks rather than halting. Build
it explicitly with the commands above.
