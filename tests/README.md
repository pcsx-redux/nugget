# tests

Every test lives in `tests/<group>/<test>/`, builds a `<test>.ps-exe` from its own `Makefile`, and is listed in `run-emulator.sh` (run or skipped, with the reason) and in `.github/scripts/hwfarm.js`. `make -C tests all PCSX_TESTS=true` builds them all; `common.mk` holds what every test shares and `runtime/` is the code linked into each one.

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
| `sio/` | serial ports: memory cards, multitap, rumble, tap cards, PocketStation, SIO1 |
| `spu/` | the SPU, and the players that drive it |
| `timers/` | the root counters |
| `tooling/` | the debugging tools: allocation tracking, msan, pcdrv, the profiler, stack bounds |

A test named after its group (`cpu/cpu`, `gte/gte`, `spu/spu`, ...) is that unit's general suite.

## Old paths

Before the move every test sat directly in `tests/`. Old name -> new path:

| Old | New |
|---|---|
| `autoinc` | `bus/delay-size-autoinc` |
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
| `gte-latency` | `gte/gte-latency` |
| `gte-latency-color` | `gte/gte-latency-color` |
| `gte-latency-dpc` | `gte/gte-latency-dpc` |
| `gte-latency-lzcs` | `gte/gte-latency-lzcs` |
| `gte-latency-math` | `gte/gte-latency-math` |
| `gte-latency-misc` | `gte/gte-latency-misc` |
| `gte-latency-mvmva` | `gte/gte-latency-mvmva` |
| `gte-latency-perspective` | `gte/gte-latency-perspective` |
| `gte-latency-singles` | `gte/gte-latency-singles` |
| `gte-math-bench` | `gte/gte-math-bench` |
| `gte-store-delay` | `gte/gte-store-delay` |
| `gte-timing` | `gte/gte-timing` |
| `gte-write-to-use` | `gte/gte-write-to-use` |
| `2mb-vram` | `gpu/2mb-vram` |
| `gpu` | `gpu/dma-chain-polygons` |
| `gpu-async-stress` | `gpu/gpu-async-stress` |
| `gpu-command` | `gpu/gpu-command` |
| `gpu-dfe-480i` | `gpu/gpu-dfe-480i` |
| `gpu-dma-dreq` | `gpu/gpu-dma-dreq` |
| `gpu-dma-loop` | `gpu/gpu-dma-loop` |
| `gpu-dma-node` | `gpu/gpu-dma-node` |
| `gpu-dma-ring` | `gpu/gpu-dma-ring` |
| `gpu-fbrect` | `gpu/gpu-fbrect` |
| `gpu-fifo` | `gpu/gpu-fifo` |
| `gpu-info8` | `gpu/gpu-info8` |
| `gpu-irq` | `gpu/gpu-irq` |
| `gpu-nop` | `gpu/gpu-nop` |
| `gpu-polyline` | `gpu/gpu-polyline` |
| `gpu-timing` | `gpu/gpu-timing` |
| `gpu-vram-move` | `gpu/gpu-vram-move` |
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
| `spu-capring` | `spu/spu-capring` |
| `spu-endmute` | `spu/spu-endmute` |
| `spu-endredirect` | `spu/spu-endredirect` |
| `spu-endx` | `spu/spu-endx` |
| `spu-envx` | `spu/spu-envx` |
| `spu-irqrefire` | `spu/spu-irqrefire` |
| `spu-konlatch` | `spu/spu-konlatch` |
| `spu-offvoice` | `spu/spu-offvoice` |
| `spu-revread` | `spu/spu-revread` |
| `spu-revstream` | `spu/spu-revstream` |
| `spu-upload-tail` | `spu/spu-upload-tail` |
| `spu-volsweep` | `spu/spu-volsweep` |
| `spu-wide-writes` | `spu/spu-wide-writes` |
| `modplayer-state` | `spu/modplayer-state` |
| `modplayer-upload-tail` | `spu/modplayer-upload-tail` |
| `cdrom` | `cdrom/cdrom` |
| `cdrom/cd-cmd-latency` | `cdrom/cd-cmd-latency` |
| `str-player-alloc` | `cdrom/str-player-alloc` |
| `dma` | `dma/dma` |
| `dma-chopping` | `dma/dma-chopping` |
| `dma-modes` | `dma/dma-modes` |
| `dma-priority` | `dma/dma-priority` |
| `mdec-roundtrip` | `mdec/mdec-roundtrip` |
| `bsdec` | `mdec/bsdec` |
| `memcard` | `sio/memcard` |
| `memcard-slot2` | `sio/memcard-slot2` |
| `multitap` | `sio/multitap` |
| `rumble` | `sio/rumble` |
| `tapcard` | `sio/tapcard` |
| `pocketstation-memmap` | `sio/pocketstation-memmap` |
| `sio-reset` | `sio/sio-reset` |
| `sio1-misc` | `sio/sio1-misc` |
| `timers` | `timers/timers` |
| `basic` | `bios/kernel-printf-strcmp` |
| `bootstate` | `bios/bootstate` |
| `loadexe` | `bios/loadexe` |
| `memcard-bios` | `bios/memcard-bios` |
| `memcard-bios-slot2` | `bios/memcard-bios-slot2` |
| `shell-480i-wait` | `bios/shell-480i-wait` |
| `psyqo` | `psyqo/psyqo` |
| `psyqo-480i-clear` | `psyqo/psyqo-480i-clear` |
| `psyqo-480i-flip` | `psyqo/psyqo-480i-flip` |
| `psyqo-dmachain` | `psyqo/psyqo-dmachain` |
| `psyqo-irq-cop2` | `psyqo/psyqo-irq-cop2` |
| `psyqo-irq-cost` | `psyqo/psyqo-irq-cost` |
| `psyqo-reinit` | `psyqo/psyqo-reinit` |
| `psyqo-spudma` | `psyqo/psyqo-spudma` |
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
