# gte-timing results

SCPH-5501 and SCPH-1001 print byte-identical output.

## Command time

Per pair of `cop2 cmd; mfc2 IR1`, 64 pairs, base (`nop; mfc2 IR1`)
subtracted. Every block totals 4 cycles under 64 times the command's time,
so `stall=14.93` is 15:

| command                  | cycles |
|--------------------------|--------|
| RTPS                     | 15     |
| RTPT                     | 23     |
| NCLIP                    | 8      |
| OP                       | 6      |
| DPCS                     | 8      |
| DPCT                     | 17     |
| DCPL                     | 8      |
| INTPL                    | 8      |
| MVMVA (rt, v0, tr)       | 8      |
| MVMVA (mx=3, ir, fc)     | 8      |
| NCS                      | 14     |
| NCT                      | 30     |
| NCCS                     | 17     |
| NCCT                     | 39     |
| NCDS                     | 19     |
| NCDT                     | 44     |
| CC                       | 11     |
| CDP                      | 13     |
| SQR                      | 5      |
| AVSZ3                    | 5      |
| AVSZ4                    | 6      |
| GPF                      | 5      |
| GPL                      | 5      |

These are the cycle counts in the psx-spx command list, all 23 of them.
NCCS reads 17.09 rather than 16.93 on both consoles: 10 cycles over the
block, not a whole cycle per command.

## What waits

`cop2 cmd; X; 64 nops` against `cop2 cmd; nop; 64 nops`, 8 repetitions.
This arm jitters by about a cycle, but the split is clear for every command:

- wait for the command: `mfc2` and `cfc2` of any register, including
  inputs the command doesn't write (VXY0, R11R12), `swc2`, and a second
  `cop2` command.
- don't wait: `mtc2`, `ctc2`, `lwc2`. `lwc2` costs about 6 cycles whatever
  the command, which is its own load from RAM.

## Gap

`cop2 cmd; N nops; mfc2 IR1`: the wait drops by one cycle per cached nop
and reaches zero once the nops cover the command, e.g. NCDT 43.5 at N=0,
35.75 at N=8, 11.75 at N=32, 0 at N=48. Nothing is charged when the
command has finished by the time it is read.

## LZCS

`mtc2 LZCS; mfc2 LZCR` costs the same as `mtc2 VXY0; mfc2 LZCR`: no wait.
