# gpu-fifo results

v0 (SCPH-1000, GP1(10h) index 7 returns stale GPUREAD) and v2 (SCPH-1001,
index 7 returns 00000002h). The 512x256 VRAM-to-VRAM copy is proven on both:
the destination pixel changes to the source colour, bit 26 stays low for
13k (v2) / 16k (v0) GPUSTAT polls, and it is still busy after every burst.

32 words written behind the copy, GPUSTAT read after each, GP1(04h)=1:

| opcodes | FIFO | v0 | v2 |
|---|---|---|---|
| A0h data words (after the 3-word header) | full after | 8 | 8 |
| 60h rectangles, word by word | full after | 12 | 12 |
| 01h, 1Fh, E1h, E2h, E6h | full after | 11 | 11 |
| 03h | full after | never (empty at end) | 11 |
| 00h, 04h..1Eh, E0h, E3h..E5h, E7h..EFh | full after | never (empty at end) | never (empty at end) |

Bit 28 (FIFO empty) reads 0 after the first write for every opcode; for the
"never" rows it is back to 1 after the 32nd write, with the copy still
running.
