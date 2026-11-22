# gpu-info8 results

v2 GPU (SCPH-1001, SCPH-5501, SCPH-7001), identical: GP1(10h) index 08h and
its mirror 18h return 00000000h whatever GPUREAD held before, under GP1(09h)
= 0, 1 and 3. Indices 06h and 09h-0Fh return nothing (GPUREAD keeps its old
value), 07h returns 00000002h.

v0 GPU (SCPH-1000): 08h-0Fh mirror 00h-07h (08h-0Ah and 0Eh-0Fh return
nothing, 0Bh-0Dh return the draw area and offset).

A GP1(10h) issued right after a GP0(E2h) can latch the texture window value
from before that GP0(E2h), so the primed values show up one row late.
