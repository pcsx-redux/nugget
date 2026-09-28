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
