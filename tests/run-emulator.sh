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

# name;ps-exe;cpu;success regex;extra flags[;timeout in seconds, default $TIMEOUT]
TESTS=(
    "kernel-printf-strcmp;bios/kernel-printf-strcmp/kernel-printf-strcmp;interpreter dynarec;$CESTER;"
    "cop0;cpu/cop0/cop0;interpreter;$CESTER;-debugger"
    "cop-branch;cpu/cop-branch/cop-branch;interpreter dynarec;$CESTER;-debugger"
    "cpu;cpu/cpu/cpu;interpreter dynarec;$CESTER;"
    "dma;dma/dma/dma;interpreter dynarec;$CESTER;-debugger"
    "gpu-command;gpu/command/gpu-command;interpreter;$CESTER;"
    "gpu-dma-dreq;gpu/dma-dreq/gpu-dma-dreq;interpreter dynarec;$CESTER;"
    "gpu-dma-loop;gpu/dma-loop/gpu-dma-loop;interpreter dynarec;$CESTER;"
    "gpu-dma-node;gpu/dma-node/gpu-dma-node;interpreter;$CESTER;"
    "gpu-fbrect;gpu/fbrect/gpu-fbrect;interpreter;$CESTER;"
    "gpu-irq;gpu/irq/gpu-irq;interpreter;$CESTER;"
    "gpu-polyline;gpu/polyline/gpu-polyline;interpreter;$CESTER;"
    "gpu-vram-move;gpu/vram-move/gpu-vram-move;interpreter;$CESTER;"
    "gpu-async-stress;gpu/async-stress/gpu-async-stress;interpreter;$CESTER;"
    "sio-reset;sio/reset/sio-reset;interpreter dynarec;^SIORST-PASS;"
    "alloc-tracker;tooling/alloc-tracker/alloc-tracker;interpreter;^alloc-tracker: all [0-9]+ checks passed;-debugger"
    "gte;gte/gte/gte;interpreter dynarec;$CESTER;"
    "gte-latency;gte/latency/gte-latency;interpreter;$CESTER;"
    "gte-latency-color;gte/latency-color/gte-latency-color;interpreter;$CESTER;"
    "gte-latency-dpc;gte/latency-dpc/gte-latency-dpc;interpreter;$CESTER;"
    "gte-latency-math;gte/latency-math/gte-latency-math;interpreter;$CESTER;"
    "gte-latency-misc;gte/latency-misc/gte-latency-misc;interpreter;$CESTER;"
    "gte-latency-mvmva;gte/latency-mvmva/gte-latency-mvmva;interpreter;$CESTER;"
    "gte-latency-perspective;gte/latency-perspective/gte-latency-perspective;interpreter;$CESTER;"
    "gte-latency-singles;gte/latency-singles/gte-latency-singles;interpreter;$CESTER;"
    "icache;cpu/icache/icache;interpreter;$CESTER;"
    "irq-branch-slot;cpu/irq-branch-slot/irq-branch-slot;interpreter dynarec;$CESTER;"
    "libc;libc/libc/libc;interpreter dynarec;$CESTER;"
    "load-timings;bus/load-timings/load-timings;interpreter;$CESTER;"
    "loadexe;bios/loadexe/loadexe;interpreter dynarec;$CESTER;"
    "memcpy;libc/memcpy/memcpy;interpreter dynarec;$CESTER;"
    "modplayer-state;spu/modplayer-state/modplayer-state;interpreter dynarec;$CESTER;"
    "memops-unroll;libc/memops-unroll/memops-unroll;interpreter;$CESTER;"
    "memcard;sio/memcard/memcard;interpreter;^memcard port 0: all [0-9]+ checks passed;;240"
    "memcard-bios;bios/memcard-bios/memcard-bios;interpreter;^memcard-bios port 0: all [0-9]+ checks passed;;120"
    "memset;libc/memset/memset;interpreter dynarec;$CESTER;"
    "pcdrv;tooling/pcdrv/pcdrv;interpreter dynarec;$CESTER;-pcdrv -pcdrvbase @HOME@"
    "psyqo;psyqo/psyqo/psyqo-tests;interpreter dynarec;^All tests passed!;"
    "psyqo-dmachain;psyqo/dmachain/dmachain;interpreter dynarec;^All 3 arms passed;"
    "psyqo-480i-clear;psyqo/480i-clear/clear480i;interpreter;^All 480i clear checks passed;"
    "psyqo-reinit;psyqo/reinit/reinit;interpreter dynarec;^All reinitialize checks passed;"
    "str-player-alloc;cdrom/str-player-alloc/str-player-alloc;interpreter dynarec;^str-player-alloc: all checks passed;"
    "psyqo-spudma;psyqo/spudma/spudma;interpreter dynarec;^All SPU DMA checks passed;"
    "rumble;sio/rumble/rumble;interpreter dynarec;$CESTER;-exec dofile('tests/sio/rumble/setup.lua')"
    "spu-volsweep;spu/volsweep/spu-volsweep;interpreter;^SPUVOLSWEEP: PASS \(0 failures\);"
    "spu-irqrefire;spu/irqrefire/spu-irqrefire;interpreter;^SPUIRQREFIRE: PASS \(0 failures\);"
    "cop0-write-keeps-registers;cpu/cop0-write-keeps-registers/cop0-write-keeps-registers;interpreter;$CESTER;"
    "timers;timers/timers/timers;interpreter dynarec;$CESTER;"
    "gpu-raster-cull-disambiguation;gpu/raster/cull-disambiguation/gpu-raster-cull-disambiguation;interpreter;^=== phase23 complete === Checks: [0-9]+ \| Passing: [1-9][0-9]* \| Failing: 0;"
)
for s in edges-clipping line-endpoints-mask-bit edge-walker-stress texture-basic cross-primitive-mask \
         edge-walker-precision gouraud-color quad-flat texture-window-8-15 lines dither abr-matrix tex-rect \
         cull-thresholds tex-window-exhaustive uv-boundary affine stride-sweep stride-sign-cross row-walk-drift \
         uv-overflow slanted-gouraud; do
    TESTS+=("gpu-raster-$s;gpu/raster/$s/gpu-raster-$s;interpreter;$CESTER;")
done
PROBE='^SUMMARY name=[^ ]+ passed=[1-9][0-9]* failed=0'
TESTS+=("2mb-vram-bank-probe-1mb;gpu/2mb-vram/bank-probe/bank-probe;interpreter;$PROBE;")
for d in bank-probe drawing-area-y drawing-offset-y gate-flip-queued primitives-cross texpage-upper transfer-h-quirk \
         transfer-wrap-y vram-blit-y vram-transfers-y; do
    TESTS+=("2mb-vram-$d;gpu/2mb-vram/$d/$d;interpreter;$PROBE;-2mbvram")
done

# ps-exe|reason
SKIPS=(
    "mdec/bsdec/bsdec|hardware rig: needs a captured bs-in.bin staged beside it"
    "cpu/bcc-bits/bcc-bits|hardware probe: the emulator does not model bus timing, prints timings, no verdict"
    "cdrom/cd-cmd-latency/cd-cmd-latency|probe: prints CD command timings, no verdict"
    "cpu/cop0-sr/cop0-sr|hardware probe: prints what each SR bit does, no verdict, and ends in the ROM through BEV"
    "cdrom/cdrom/cdrom|needs the disc from cdrom/cdrom/create-test-iso.lua and the CD-ROM code from pcsx-redux#1129"
    "dma/modes/dma-modes|hardware probe: prints DMA register states per sync mode, loops forever, no verdict"
    "dma/priority/dma-priority|hardware probe: prints DMA arbitration timings, loops forever, no verdict"
    "spu/wide-writes/spu-wide-writes|hardware probe: prints SPU write miss counts, loops forever, no verdict"
    "cpu/dcache/dcache|emulator gap: the BIU d-cache-as-scratchpad modes are not modelled"
    "sio/sio1-misc/sio1-misc|emulator gap: SIO1 1F80105Ch is not modelled (pcsx-redux#2445)"
    "gpu/dma-chain-polygons/dma-chain-polygons|probe: loops forever, no verdict"
    "gpu/nop/gpu-nop|probe: loops forever, no verdict"
    "gpu/fifo/gpu-fifo|hardware probe: prints FIFO readings, no verdict"
    "gte/latency-lzcs/gte-latency-lzcs|emulator gap: the GTE store delay is not modelled, so the cached negative control reads correct at N=0"
    "gte/math-bench/gte-math-bench|benchmark: prints timings, no verdict"
    "gte/write-to-use/gte-write-to-use|emulator gap: GTE write-to-use delay per command and register not in the dev AppImage; run by the pcsx-redux gtests"
    "gpu/dfe-480i/gpu-dfe-480i|emulator gap: GP0(E1h).10 row skipping in 480i not in the dev AppImage; run by the pcsx-redux gtests"
    "gpu/gpustat-texpage-bits/gpustat-texpage-bits|emulator gap: the dev AppImage clears GPUSTAT.10 on a textured polygon; run by the pcsx-redux gtests"
    "gpu/dma-ring/gpu-dma-ring|emulator gap: a GPU linked list used as a ring needs GPU timing and back-pressure; Redux ends the loop at once (every assert is hardware-only)"
    "gpu/timing/gpu-timing|emulator gap: GPU draw and transfer time per primitive; Redux has no per-pixel GPU timing (the size-growth asserts are hardware-only)"
    "gpu/gpustat-480i/gpustat-480i|emulator gap: GPUSTAT bits 13 and 31 by beam position in 480i not in the dev AppImage; run by the pcsx-redux gtests"
    "gpu/gpustat-timeline/gpustat-timeline|emulator gap: GPUSTAT bits 13 and 31 outside NTSC 480i and across interlace switches not in the dev AppImage; run by the pcsx-redux gtests"
    "gpu/gpustat-cmd-ready/gpustat-cmd-ready|emulator gap: GPUSTAT bit 26 low mid-command not in the dev AppImage; run by the pcsx-redux gtests"
    "psyqo/irq-cop2/irqcop2|takeover probe: psyqo owns the kernel, results over polled SIO1, spins at the end; run by hand on hardware"
    "psyqo/irq-cost/irqcost|takeover probe: psyqo owns the kernel, prints VBlank interrupt costs over polled SIO1, no verdict"
    "psyqo/480i-flip/flip480i|emulator gap: GP0(E1h).10 row skipping in 480i not in the dev AppImage; run by the pcsx-redux gtests"
    "bios/shell-480i-wait/shell-480i-wait|emulator gap: GP0(E1h).10 row skipping in 480i not in the dev AppImage; run by the pcsx-redux gtests"
    "cpu/mult-timing/mult-timing|hardware probe: the emulator does not model multiply latency, prints timings, no verdict"
    "gte/timing/gte-timing|hardware probe: prints GTE command timings, no verdict"
    "sio/pocketstation-memmap/pocketstation-memmap|hardware probe: needs a PocketStation in a card slot, prints readings, no verdict"
    "spu/spu/spu|needs the SPU from pcsx-redux#2077 in the dev AppImage"
    "spu/endx/spu-endx|needs the SPU from pcsx-redux#2077 in the dev AppImage"
    "spu/endmute/spu-endmute|needs the ENVX end-block fix in pcsx-redux"
    "spu/endredirect/spu-endredirect|needs the end-block redirect from pcsx-redux#2355 in the dev AppImage"
    "spu/konlatch/spu-konlatch|needs the key-on timeline in pcsx-redux"
    "spu/offvoice/spu-offvoice|needs the SPU from pcsx-redux#2077 in the dev AppImage"
    "spu/upload-tail/spu-upload-tail|hardware probe: prints per-arm tail loss across wait strategies, verdict only on its own controls"
    "spu/modplayer-upload-tail/modplayer-upload-tail|hardware probe: prints per-arm tail loss across wait strategies, verdict only on its own controls"
    "bus/mmio-partial-writes/mmio-partial-writes|probe: loops forever, no verdict"
    "bus/delay-size-autoinc/delay-size-autoinc|hardware probe: prints what Delay/Size bit 13 does to split accesses, no verdict"
    "cpu/bcc-narrow/bcc-narrow|hardware probe: prints BCC narrow-read results, no verdict"
    "dma/chopping/dma-chopping|hardware probe: prints CPU progress during a chopped DMA2 transfer, no verdict"
    "bus/exp2-width/exp2-width|hardware probe: prints every access width to the EXP2 region, no verdict"
    "gpu/info8/gpu-info8|hardware probe: prints GP1(10h) index 08h readings, no verdict"
    "mdec/roundtrip/mdec-roundtrip|rig: needs a job file over pcdrv, writes the decoded output back, no verdict"
    "gpu/scanline-timing/scanline-timing|hardware probe: prints scanline and frame lengths, loops forever, no verdict"
    "spu/capring/spu-capring|hardware probe: prints capture ring readings, no verdict"
    "spu/envx/spu-envx|hardware probe: prints ENVX readings beside the expected values, always exits 0"
    "spu/revread/revread|hardware probe: graded on the digital audio tap, loops forever, no verdict"
    "spu/revstream/revstream|reverb stimulus for the digital audio tap: loops forever, no verdict"
    "tooling/msan/msan|needs the msan fix from pcsx-redux#2203 in the dev AppImage"
    "tooling/msan-trip/msan-trip|expects exit 1, run by the pcsx-redux gtests"
    "tooling/stack-bounds/stack-bounds|needs the stack bounds command in the dev AppImage; run by the pcsx-redux gtests"
    "tooling/stack-bounds-churn/stack-bounds-churn|needs the stack bounds command in the dev AppImage; run by the pcsx-redux gtests"
    "tooling/stack-bounds-trip/stack-bounds-trip|expects exit 251, run by the pcsx-redux gtests"
    "tooling/stack-bounds-heap/stack-bounds-heap|expects exit 251, run by the pcsx-redux gtests"
    "tooling/stack-bounds-heap-quiet/stack-bounds-heap-quiet|needs the stack bounds command in the dev AppImage; run by the pcsx-redux gtests"
    "tooling/profiler/profiler|fixture for the call-path profiler, checks nothing itself; run by the pcsx-redux gtests"
    "sio/memcard-slot2/memcard-slot2|the memcard test built for slot 2, for the farm console whose test card is there"
    "bios/memcard-bios-slot2/memcard-bios-slot2|the memcard-bios test built for slot 2, for the farm console whose test card is there"
    "tooling/pcdrv-net/pcdrv-net|needs a TCP echo peer on 127.0.0.1:37373, run by the pcsx-redux gtests"
    "tooling/pcdrv-net-off/pcdrv-net-off|needs PCdrv networking from pcsx-redux#2327 in the dev AppImage"
    "sio/multitap/multitap|needs -shmdisplay host input and multitap from pcsx-redux#2471, run by the pcsx-redux gtests"
    "sio/tapcard/tapcard|needs a setup-*.lua arm and tap memory cards in pcsx-redux, run by the pcsx-redux gtests"
)
for d in display-area-y fast-fill-h-quirk fast-fill-y gp1-09-matrix; do
    SKIPS+=("gpu/2mb-vram/$d/$d|probe for 2MB-VRAM hardware: loops forever, no verdict")
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
    local name=$1 exe=$2 cpu=$3 token=$4 extra=$5 limit=${6:-$TIMEOUT}
    local log="$LOGS/$name-$cpu.log"
    local t0=$SECONDS
    # A config dir each: concurrent first launches racing to create the
    # shared one, and its memory cards, hang some of them at boot.
    local home="$LOGS/home-$name-$cpu"
    mkdir -p "$home"
    extra=${extra//@HOME@/$home}
    # shellcheck disable=SC2086
    HOME="$home" XDG_CONFIG_HOME="$home/.config" timeout -k 10 "$limit" "$EMU" -no-ui -run -stdout -testmode "-$cpu" \
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
    IFS=';' read -r name exe cpus token extra limit <<< "$t"
    for cpu in $cpus; do jobs+=("$name;$exe;$cpu;$token;$extra;$limit"); done
done

# Streamed as each run finishes, so a stuck run is visible while it is stuck.
printf '%s\n' "${jobs[@]}" | xargs -P "$JOBS" -d '\n' -I{} bash -c '
    IFS=";" read -r name exe cpu token extra limit <<< "$1"
    read -ra BIOSFLAG <<< "$BIOSFLAG_STR"
    run_one "$name" "$exe" "$cpu" "$token" "$extra" "$limit"
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
