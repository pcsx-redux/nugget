#!/bin/bash
# Run the test suite's ps-exes in PCSX-Redux and grade each on a positive
# success line, never on the absence of a failure one.
#
#   tests/run-emulator.sh <path to pcsx-redux or its AppRun> [openbios.bin]
#
# Run from the repository root, after `make -C tests all PCSX_TESTS=true`.
# Every ps-exe under tests/ has to be listed below, either as a test or as
# a skip with its reason; one that is neither fails the run, so a new test
# cannot be left out of CI by accident.

set -u

# SDL wants a display even under -testmode. One X server for the whole run:
# parallel `xvfb-run -a` instances race for the same display number, and the
# loser's emulator dies with exit 1 when the winner tears its server down.
if [ -z "${DISPLAY:-}" ]; then
    exec xvfb-run -a "$0" "$@"
fi

EMU=${1:?usage: $0 <pcsx-redux> [openbios.bin]}
BIOS=${2:-openbios/openbios.bin}
JOBS=${JOBS:-$(nproc)}
TIMEOUT=${TIMEOUT:-60}

CESTER='^Synthesis: SUCCESS Tests: [0-9]+ \| Passing: [1-9][0-9]* \| Failing: 0'

# name;ps-exe;cpu;success regex;extra flags
TESTS=(
    "basic;basic/basic;interpreter dynarec;$CESTER;"
    "cop0;cop0/cop0;interpreter;$CESTER;-debugger"
    "cpu;cpu/cpu;interpreter dynarec;$CESTER;"
    "dma;dma/dma;interpreter dynarec;$CESTER;-debugger"
    "gte;gte/gte;interpreter dynarec;$CESTER;"
    "gte-latency;gte-latency/gte-latency;interpreter;$CESTER;"
    "gte-latency-color;gte-latency-color/gte-latency-color;interpreter;$CESTER;"
    "gte-latency-dpc;gte-latency-dpc/gte-latency-dpc;interpreter;$CESTER;"
    "gte-latency-math;gte-latency-math/gte-latency-math;interpreter;$CESTER;"
    "gte-latency-misc;gte-latency-misc/gte-latency-misc;interpreter;$CESTER;"
    "gte-latency-mvmva;gte-latency-mvmva/gte-latency-mvmva;interpreter;$CESTER;"
    "gte-latency-perspective;gte-latency-perspective/gte-latency-perspective;interpreter;$CESTER;"
    "gte-latency-singles;gte-latency-singles/gte-latency-singles;interpreter;$CESTER;"
    "libc;libc/libc;interpreter dynarec;$CESTER;"
    "load-timings;load-timings/load-timings;interpreter;$CESTER;"
    "memcpy;memcpy/memcpy;interpreter dynarec;$CESTER;"
    "memops-unroll;memops-unroll/memops-unroll;interpreter;$CESTER;"
    "memset;memset/memset;interpreter dynarec;$CESTER;"
    "pcdrv;pcdrv/pcdrv;interpreter dynarec;$CESTER;-pcdrv -pcdrvbase @HOME@"
    "psyqo;psyqo/psyqo-tests;interpreter dynarec;^All tests passed!;"
    "psyqo-dmachain;psyqo-dmachain/dmachain;interpreter dynarec;^All 3 arms passed;"
    "timers;timers/timers;interpreter dynarec;$CESTER;"
    "gpu-raster-phase23;gpu-raster-phase23/gpu-raster-phase23;interpreter;^=== phase23 complete === Checks: [0-9]+ \| Passing: [1-9][0-9]* \| Failing: 0;"
)
for p in $(seq 1 22); do
    TESTS+=("gpu-raster-phase$p;gpu-raster-phase$p/gpu-raster-phase$p;interpreter;$CESTER;")
done

# ps-exe|reason
SKIPS=(
    "bsdec/bsdec|hardware rig: needs a captured bs-in.bin staged beside it"
    "cdrom/cdrom|needs the disc from cdrom/create-test-iso.lua and the CD-ROM code from pcsx-redux#1129"
    "dcache/dcache|emulator gap: the BIU d-cache-as-scratchpad modes are not modelled"
    "gpu/gpu|probe: loops forever, no verdict"
    "gpu-nop/gpu-nop|probe: loops forever, no verdict"
    "gte-latency-lzcs/gte-latency-lzcs|emulator gap: the GTE store delay is not modelled, so the cached negative control reads correct at N=0"
    "gte-math-bench/gte-math-bench|benchmark: prints timings, no verdict"
    "spu/spu|needs the SPU from pcsx-redux#2077 in the dev AppImage"
    "spu-endx/spu-endx|needs the SPU from pcsx-redux#2077 in the dev AppImage"
    "spu-offvoice/spu-offvoice|needs the SPU from pcsx-redux#2077 in the dev AppImage"
    "regwrites/regwrites|probe: loops forever, no verdict"
)
for d in bank-probe display-area-y drawing-area-y drawing-offset-y fast-fill-h-quirk fast-fill-y \
         gp1-09-matrix primitives-cross transfer-h-quirk vram-blit-y vram-transfers-y; do
    SKIPS+=("2mb-vram/$d/$d|probe for 2MB-VRAM hardware: loops forever, no verdict")
done

LOGS=$(mktemp -d)
fail=0

declare -A known
for t in "${TESTS[@]}"; do IFS=';' read -r _ exe _ <<< "$t"; known[$exe]=1; done
for s in "${SKIPS[@]}"; do known[${s%%|*}]=1; done
while read -r f; do
    f=${f#tests/}; f=${f%.ps-exe}
    if [ -z "${known[$f]:-}" ]; then
        echo "UNLISTED: tests/$f.ps-exe is neither run nor skipped in $0"
        fail=1
    fi
done < <(find tests -name '*.ps-exe' | sort)
for k in "${!known[@]}"; do
    if [ ! -f "tests/$k.ps-exe" ]; then
        echo "MISSING: tests/$k.ps-exe was not built"
        fail=1
    fi
done

BIOSFLAG=(-bios "$BIOS")

run_one() {
    local name=$1 exe=$2 cpu=$3 token=$4 extra=$5
    local log="$LOGS/$name-$cpu.log"
    local t0=$SECONDS
    # A config dir each: concurrent first launches racing to create the
    # shared one, and its memory cards, hang some of them at boot.
    local home="$LOGS/home-$name-$cpu"
    mkdir -p "$home"
    extra=${extra//@HOME@/$home}
    # shellcheck disable=SC2086
    HOME="$home" XDG_CONFIG_HOME="$home/.config" timeout -k 10 "$TIMEOUT" "$EMU" -no-ui -run -stdout -testmode "-$cpu" \
        "${BIOSFLAG[@]}" $extra -loadexe "tests/$exe.ps-exe" > "$log" 2>&1
    local rc=$?
    local line
    line=$(sed 's/\x1b\[[0-9;]*m//g' "$log" | tr -d '\r' | grep -aE "$token" | tail -1)
    if [ $rc -eq 0 ] && [ -n "$line" ]; then
        echo "PASS $name ($cpu, $((SECONDS - t0))s): $line"
    else
        # One write, so parallel jobs cannot interleave into it.
        printf 'FAIL %s (%s, %ss): exit %s, no success line\n%s\n' "$name" "$cpu" \
            "$((SECONDS - t0))" "$rc" "$(sed 's/\x1b\[[0-9;]*m//g' "$log" | tail -40 | sed 's/^/    /')"
        return 1
    fi
}
export -f run_one
export EMU TIMEOUT LOGS
export BIOSFLAG_STR="${BIOSFLAG[*]}"

jobs=()
for t in "${TESTS[@]}"; do
    IFS=';' read -r name exe cpus token extra <<< "$t"
    for cpu in $cpus; do jobs+=("$name;$exe;$cpu;$token;$extra"); done
done

# Streamed as each run finishes, so a stuck run is visible while it is stuck.
printf '%s\n' "${jobs[@]}" | xargs -P "$JOBS" -d '\n' -I{} bash -c '
    IFS=";" read -r name exe cpu token extra <<< "$1"
    read -ra BIOSFLAG <<< "$BIOSFLAG_STR"
    run_one "$name" "$exe" "$cpu" "$token" "$extra"
' _ {} | tee "$LOGS/results"

ran=${#jobs[@]}
passed=$(grep -c '^PASS ' "$LOGS/results")
echo
echo "=== SKIPPED (${#SKIPS[@]}) ==="
for s in "${SKIPS[@]}"; do echo "  tests/${s%%|*}.ps-exe: ${s#*|}"; done
echo
echo "=== $passed/$ran runs passed, ${#TESTS[@]} suites run, ${#SKIPS[@]} skipped, $(find tests -name '*.ps-exe' | wc -l) built ==="
[ "$passed" -eq "$ran" ] || fail=1
rm -rf "$LOGS"
exit $fail
