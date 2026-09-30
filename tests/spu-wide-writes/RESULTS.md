# spu-wide-writes results

SCPH-5501 and SCPH-1001 agree on everything but the rate of isolated misses.
Random values throughout. Trials per arm: `sw` 8192, `sh` 8192 (two writes
each), delayed readback 2048, `sb` control 1024, and "burst" 1024 runs of
eight back-to-back `sw` to 1F801DC0h..1F801DDCh.

With SPU_DELAY (1F801014h) at the BIOS value 200931E1h:

| arm | low half lost | high half lost |
|---|---|---|
| `sw`, isolated | 0..6 on SCPH-5501, 2..2457 on SCPH-1001 | 0 |
| two `sh` | 0 | 0 |
| `sw`, read back after a delay | 0..1 | 0 |
| burst of eight `sw` | exactly 1024, one per burst | 0 |
| `sb` to byte 1 (positive control, known dropped) | 1024 of 1024 | 0 |

A lost half reads back its previous value on the first read and after a
delay, so the write never landed; it is not late. The slot that loses its
low half in a burst varies.

Other SPU_DELAY values, `sw` isolated and burst:

| SPU_DELAY | result |
|---|---|
| 200931EFh (write delay 15) | no misses |
| 200931FFh | no misses |
| 200933E1h (bit 9 set) | no misses |
| 200935E1h (bit 10 set) | about 300 isolated low-half misses, one per burst |
| 200930E1h (bit 8 clear) | every high half lost, low halves fine |
