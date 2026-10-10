# gte-store-delay results

SCPH-1000, SCPH-1001, SCPH-5501 and SCPH-7001, identical, 3 of 3 repetitions.
N is the number of nops between the write and the use.

| arm   | N=0 | N=1 | N=2 | N=3 | N=4 |
|-------|-----|-----|-----|-----|-----|
| SQR_M | 3   | 4   | 9   | 9   | 9   |
| SQR_L | 3   | 4   | 9   | 9   | 9   |
| OP_C  | 5   | 5   | 7   | 7   | 7   |
| RD_M  | 2   | 2   | 3   | 3   | 3   |
| RD_C  | 5   | 5   | 7   | 7   | 7   |
| SW_M  | 2   | 2   | 3   | 3   | 3   |

A use with two or more instructions between it and the mtc2, ctc2 or lwc2
sees the new value; with zero or one it sees the old one, whether the use is
a command, mfc2, cfc2 or swc2. With N=0 SQR squares the old IR1 (4) and IR1
then holds the written 3, not the result.
