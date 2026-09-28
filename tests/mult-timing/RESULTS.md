# mult-timing results

SCPH-5501 and SCPH-1000 print byte-identical output. Per pair of
`mult; mflo`, 64 pairs, base (nop; mflo) subtracted:

| rs                      | mult | multu |
|-------------------------|------|-------|
| 00000000h..000007FFh    | 6    | 6     |
| 00000800h..000FFFFFh    | 9    | 9     |
| 00100000h..7FFFFFFFh    | 13   | 13    |
| FFFFF800h..FFFFFFFFh    | 6    | 13    |
| FFF00000h..FFFFF7FFh    | 9    | 13    |
| 80000000h..FFEFFFFFh    | 13   | 13    |

Every boundary value was measured on both sides (7FFh/800h, FFFFFh/100000h,
FFFFF7FFh/FFFFF800h/FFFFF801h, FFEFFFFFh/FFF00000h/FFF00001h). With rs=5,
every rt in the list takes the Fast time: only rs selects the class.

## Busy unit, mtlo/mthi, div

SCPH-5501 and SCPH-1000 print identical rows. Stall per pair of
`op1; op2; mflo`, base (nop; nop; mflo) subtracted:

| op1, op2                       | stall |
|--------------------------------|-------|
| mult slow; mult slow           | 13    |
| mult fast; mult slow           | 6     |
| mult slow; mult fast (rs=0)    | 6     |
| div; mult fast                 | 6     |
| mult slow or fast; div         | 36    |
| mult slow; mtlo                | 0     |
| mult slow; mthi                | 0     |

A mult or div issued while the unit is busy replaces the running operation:
the stall is the second operation's own time, never the sum.

`mult 12345678h,12345678h` (product 014B66DCh:1DF4D840h) then, with no gap:

| after     | lo        | hi        |
|-----------|-----------|-----------|
| mtlo k    | k         | 00000000h |
| mthi k    | 12345678h | k         |

With rs=5: mtlo leaves hi=00000001h (product hi is 0), mthi leaves lo=5.
With 32 nops between mult and mtlo, hi holds the product's hi. So mtlo and
mthi abort a busy multiply and leave the other register partially computed.

div and divu stall 36 for every rs in {12345678h, 7FFFFFFFh, 80000000h,
FFFFFFFFh, 1, 0} against every rt in {3, 1, FFFFFFFFh, 7FFFFFFFh, 10000h, 0}.
