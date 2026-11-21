# spu-revread results

SCPH-1001, 2026-10-07, I2S tap on the final mix (BCLK/DATO/LRCK at 16 MHz), one
capture window per arm. Left is the louder channel (vLOUT 0x4000, vROUT 0x2000).
"Repeats" compares each sample with the one a work-area pass earlier (8192
samples at 44.1 kHz).

| Arm   | SPUCNT bit 7 | vLOUT/vROUT   | L peak / RMS | R peak / RMS | Repeats every pass |
|-------|--------------|---------------|--------------|--------------|--------------------|
| ARM1A | 0            | 4000h / 2000h | 8508 / 3984  | 4254 / 1992  | 100.00% bit-exact  |
| ARM3  | 0            | 0 / 0         | 0 / 0        | 0 / 0        | all zero           |
| ARM1B | 0            | 4000h / 2000h | 8508 / 3983  | 4254 / 1991  | 100.00% bit-exact  |
| ARM2  | set mid-window | 4000h / 2000h | pattern, then 0 from 1112 ms on | same | -       |

With bit 7 clear the work area still reaches the output, and the work area is
not written: the pattern is unchanged 7 seconds later, and with these
coefficients a write stores zero, which would clear it within one pass (186 ms).
Once bit 7 is set the output drops to zero.

Redux at 4f0956d8: silent in ARM1A/ARM1B. With grumpycoders/pcsx-redux#2239
applied it plays the pattern at the same level (L RMS 3957), normalized
correlation 0.962 against silicon; about half the samples match exactly.
