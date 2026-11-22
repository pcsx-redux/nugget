# tests

Every test lives in `tests/<group>/<test>/`, builds a ps-exe from its own `Makefile`, and is listed in `run-emulator.sh` (run or skipped, with the reason) and in `.github/scripts/hwfarm.js`. `make -C tests all PCSX_TESTS=true` builds them all; `common.mk` holds what every test shares and `runtime/` is the code linked into each one.

| Group | What goes in it |
|---|---|
| `bios/` | the kernel and BIOS services: exe loading, boot state, BIOS memory card calls, the shell |
| `bus/` | the memory control registers and bus behaviour: DEV2/DEV8/EXP2 widths, Delay/Size, MMIO write masking, load timings |
| `cdrom/` | the CD-ROM controller and what streams from it |
| `cpu/` | the R3000A and cop0: branches, delay slots, caches, SR/Cause, multiply timing |
| `dma/` | the DMA controller itself; GPU-side DMA tests sit in `gpu/` |
| `gpu/` | the GPU: commands, GPUSTAT, FIFO, DMA to the GPU, timing, 2MB VRAM |
| `gpu/raster/` | rasterizer characterization, one directory per subject; shared helpers in `gpu/raster/common/` |
| `gte/` | the GTE: instructions, latencies, store and write-to-use delays |
| `libc/` | the C library: heap, string functions, memcpy/memset and their unroll sweep |
| `mdec/` | the MDEC and the BS decoder |
| `psyqo/` | tests that build against psyqo and check psyqo's own behaviour |
| `pocketstation/` | the PocketStation itself: its ARM CPU, memory map, IR and LCD, reached from the PS1 through the memory card port |
| `sio/` | serial ports: memory cards, multitap, rumble, tap cards, SIO1 |
| `spu/` | the SPU, and the players that drive it |
| `timers/` | the root counters |
| `tooling/` | the debugging tools: allocation tracking, msan, pcdrv, the profiler, stack bounds |

A test named after its group (`cpu/cpu`, `gte/gte`, `spu/spu`, ...) is that unit's general suite. Directories do not repeat the group (`gpu/fifo`), but the ps-exe keeps a name unique across the tree (`gpu/fifo/gpu-fifo.ps-exe`, `gpu/raster/lines/gpu-raster-lines.ps-exe`), since the run lists and the hardware farm key on it.

## Old paths

Before the move every test sat directly in `tests/`. Old name -> new path:

| Old | New |
|---|---|
| `autoinc` | `bus/delay-size-autoinc` |
| `dev-wide` | `bus/dev-wide` |
| `dev8-readback` | `bus/dev8-readback` |
| `exp2-width` | `bus/exp2-width` |
| `load-timings` | `bus/load-timings` |
| `regwrites` | `bus/mmio-partial-writes` |
| `bcc-bits` | `cpu/bcc-bits` |
| `bcc-narrow` | `cpu/bcc-narrow` |
| `cop-branch` | `cpu/cop-branch` |
| `cop0` | `cpu/cop0` |
| `cop0-sr` | `cpu/cop0-sr` |
| `cpu` | `cpu/cpu` |
| `dcache` | `cpu/dcache` |
| `icache` | `cpu/icache` |
| `irq-branch-slot` | `cpu/irq-branch-slot` |
| `mult-timing` | `cpu/mult-timing` |
| `sr-write` | `cpu/cop0-write-keeps-registers` |
| `gte` | `gte/gte` |
| `gte-latency` | `gte/latency` |
| `gte-latency-color` | `gte/latency-color` |
| `gte-latency-dpc` | `gte/latency-dpc` |
| `gte-latency-lzcs` | `gte/latency-lzcs` |
| `gte-latency-math` | `gte/latency-math` |
| `gte-latency-misc` | `gte/latency-misc` |
| `gte-latency-mvmva` | `gte/latency-mvmva` |
| `gte-latency-perspective` | `gte/latency-perspective` |
| `gte-latency-singles` | `gte/latency-singles` |
| `gte-math-bench` | `gte/math-bench` |
| `gte-store-delay` | `gte/store-delay` |
| `gte-timing` | `gte/timing` |
| `gte-write-to-use` | `gte/write-to-use` |
| `2mb-vram` | `gpu/2mb-vram` |
| `gpu` | `gpu/dma-chain-polygons` |
| `gpu-async-stress` | `gpu/async-stress` |
| `gpu-command` | `gpu/command` |
| `gpu-dfe-480i` | `gpu/dfe-480i` |
| `gpu-dma-forced` | `gpu/dma-forced` |
| `gpu-dma-dreq` | `gpu/dma-dreq` |
| `gpu-dma-loop` | `gpu/dma-loop` |
| `gpu-dma-node` | `gpu/dma-node` |
| `gpu-dma-ring` | `gpu/dma-ring` |
| `gpu-fbrect` | `gpu/fbrect` |
| `gpu-fifo` | `gpu/fifo` |
| `gpu-info8` | `gpu/info8` |
| `gpu-irq` | `gpu/irq` |
| `gpu-nop` | `gpu/nop` |
| `gpu-polyline` | `gpu/polyline` |
| `gpu-timing` | `gpu/timing` |
| `gpu-vram-move` | `gpu/vram-move` |
| `gpustat-480i` | `gpu/gpustat-480i` |
| `gpustat-timeline` | `gpu/gpustat-timeline` |
| `gpustat10` | `gpu/gpustat-texpage-bits` |
| `gpustat26` | `gpu/gpustat-cmd-ready` |
| `scanline-timing` | `gpu/scanline-timing` |
| `gpu-raster-phase1` | `gpu/raster/edges-clipping` |
| `gpu-raster-phase2` | `gpu/raster/line-endpoints-mask-bit` |
| `gpu-raster-phase3` | `gpu/raster/edge-walker-stress` |
| `gpu-raster-phase4` | `gpu/raster/texture-basic` |
| `gpu-raster-phase5` | `gpu/raster/cross-primitive-mask` |
| `gpu-raster-phase6` | `gpu/raster/edge-walker-precision` |
| `gpu-raster-phase7` | `gpu/raster/gouraud-color` |
| `gpu-raster-phase8` | `gpu/raster/quad-flat` |
| `gpu-raster-phase9` | `gpu/raster/texture-window-8-15` |
| `gpu-raster-phase10` | `gpu/raster/lines` |
| `gpu-raster-phase11` | `gpu/raster/dither` |
| `gpu-raster-phase12` | `gpu/raster/abr-matrix` |
| `gpu-raster-phase13` | `gpu/raster/tex-rect` |
| `gpu-raster-phase14` | `gpu/raster/cull-thresholds` |
| `gpu-raster-phase15` | `gpu/raster/tex-window-exhaustive` |
| `gpu-raster-phase16` | `gpu/raster/uv-boundary` |
| `gpu-raster-phase17` | `gpu/raster/affine` |
| `gpu-raster-phase18` | `gpu/raster/stride-sweep` |
| `gpu-raster-phase19` | `gpu/raster/stride-sign-cross` |
| `gpu-raster-phase20` | `gpu/raster/row-walk-drift` |
| `gpu-raster-phase21` | `gpu/raster/uv-overflow` |
| `gpu-raster-phase22` | `gpu/raster/slanted-gouraud` |
| `gpu-raster-phase23` | `gpu/raster/cull-disambiguation` |
| `spu` | `spu/spu` |
| `spu-capring` | `spu/capring` |
| `spu-endmute` | `spu/endmute` |
| `spu-endredirect` | `spu/endredirect` |
| `spu-endx` | `spu/endx` |
| `spu-envx` | `spu/envx` |
| `spu-irqrefire` | `spu/irqrefire` |
| `spu-konlatch` | `spu/konlatch` |
| `spu-offvoice` | `spu/offvoice` |
| `spu-revread` | `spu/revread` |
| `spu-revstream` | `spu/revstream` |
| `spu-upload-tail` | `spu/upload-tail` |
| `spu-volsweep` | `spu/volsweep` |
| `spu-wide-writes` | `spu/wide-writes` |
| `modplayer-state` | `spu/modplayer-state` |
| `modplayer-upload-tail` | `spu/modplayer-upload-tail` |
| `cdrom` | split by area: `cdrom/commands`, `cdrom/reading`, `cdrom/playback`, `cdrom/race`, `cdrom/errors`; shared code in `cdrom/common` |
| `cdrom/cd-cmd-latency` | `cdrom/cd-cmd-latency` |
| `str-player-alloc` | `cdrom/str-player-alloc` |
| `dma` | `dma/dma` |
| `dma-chopping` | `dma/chopping` |
| `dma-modes` | `dma/modes` |
| `dma-priority` | `dma/priority` |
| `mdec-roundtrip` | `mdec/roundtrip` |
| `bsdec` | `mdec/bsdec` |
| `memcard` | `sio/memcard` |
| `memcard-slot2` | `sio/memcard-slot2` |
| `multitap` | `sio/multitap` |
| `rumble` | `sio/rumble` |
| `tapcard` | `sio/tapcard` |
| `pocketstation-memmap` | `pocketstation/memmap` |
| `sio-reset` | `sio/reset` |
| `sio1-misc` | `sio/sio1-misc` |
| `timers` | `timers/timers` |
| `basic` | `bios/kernel-printf-strcmp` |
| `bootstate` | `bios/bootstate` |
| `loadexe` | `bios/loadexe` |
| `memcard-bios` | `bios/memcard-bios` |
| `memcard-bios-slot2` | `bios/memcard-bios-slot2` |
| `shell-480i-wait` | `bios/shell-480i-wait` |
| `psyqo` | `psyqo/psyqo` |
| `psyqo-480i-clear` | `psyqo/480i-clear` |
| `psyqo-480i-flip` | `psyqo/480i-flip` |
| `psyqo-dmachain` | `psyqo/dmachain` |
| `psyqo-irq-cop2` | `psyqo/irq-cop2` |
| `psyqo-irq-cost` | `psyqo/irq-cost` |
| `psyqo-reinit` | `psyqo/reinit` |
| `psyqo-spudma` | `psyqo/spudma` |
| `libc` | `libc/libc` |
| `memcpy` | `libc/memcpy` |
| `memset` | `libc/memset` |
| `memops-unroll` | `libc/memops-unroll` |
| `alloc-tracker` | `tooling/alloc-tracker` |
| `msan` | `tooling/msan` |
| `msan-trip` | `tooling/msan-trip` |
| `pcdrv` | `tooling/pcdrv` |
| `pcdrv-net` | `tooling/pcdrv-net` |
| `pcdrv-net-off` | `tooling/pcdrv-net-off` |
| `profiler` | `tooling/profiler` |
| `stack-bounds` | `tooling/stack-bounds` |
| `stack-bounds-churn` | `tooling/stack-bounds-churn` |
| `stack-bounds-heap` | `tooling/stack-bounds-heap` |
| `stack-bounds-heap-quiet` | `tooling/stack-bounds-heap-quiet` |
| `stack-bounds-trip` | `tooling/stack-bounds-trip` |
| `support` | `runtime` (not a test: the code linked into every test) |

`gpu-raster-phase1/raster-helpers.h` is now `gpu/raster/common/raster-helpers.h`. Renamed tests build an exe of the new name, `gpu/raster/<subject>/gpu-raster-<subject>.ps-exe` for the rasterizer phases.
