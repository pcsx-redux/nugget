# dma-priority results

SCPH-5501 and SCPH-1001 agree. Cycles on root counter 2 at the system clock.

## CPU against a transfer

| transfer | work loop | stall | loop (alone) | done |
|---|---|---|---|---|
| DMA6 burst, 4096 words | uncached RAM loads | 4358 | 1821 (1821) | 6241 |
| DMA6 burst | cached ALU loop | 4358 | 1803 (1803) | 6223 |
| DMA6 burst | scratchpad loads | 4358 | 260 (260) | 4682 |
| DMA6 chopped, 16 words / 16 cycles | uncached RAM loads | 4353 | 1821 (1819) | 6238 |
| DMA2 sync mode 1, 1024 words | uncached RAM loads | 32 | 3060 (1821) | 3245 |

Each figure is the same, within noise, for CPU priority 0 against channel
priority 7, 7 against 0, and 3 against 3. The CPU priority field moved
nothing.

A counter read issued within 3 cycles of the CHCR store that starts DMA6
completes first (stall 6..8), and the next RAM load then waits for the whole
transfer (loop about 6170). From 4 nops on, the counter read itself waits.

## DMA2 against DMA6

Both started with their master enables clear, then enabled by one DPCR write.
Nothing moves while the enables are clear.

| DMA2 prio | DMA6 prio | result |
|---|---|---|
| alone | - | 1024 words of VRAM data |
| - | alone | 1024 words of OT |
| 3 | 7 | DMA2 runs 80..96 words, DMA6 runs to completion, DMA2 finishes |
| 0 | 7 | same, 80..112 words |
| 7 | 0 | DMA6 completes before DMA2 starts |
| 3 | 3 | DMA6 completes before DMA2 starts |
| 0 | 0 | DMA6 completes before DMA2 starts |

The cut-in point varies between runs, which is why those two rows print
UNSTABLE.
