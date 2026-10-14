# gte-write-to-use results

SCPH-1000, SCPH-1001, SCPH-5501 and SCPH-7001: the cached columns are identical on all four,
and the two cached passes agree with each other. The uncached column is SCPH-5501; it differs
elsewhere in one cell (CC IR2, NNNN on the other three).

Letters, for N = 0..3 nops between the write and the command: O / N the outputs match the
old / new value, o / n the same with the written register left out (the late write landed
over the command's own result), X some outputs old and some new.
"min N" is the smallest N from which every cached result is new.


## NCCT

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN |
| L11L12 | 0 | NNNN | NNNN |
| L13L21 | 0 | NNNN | NNNN |
| L22L23 | 0 | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN |

## NCT

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN |
| L11L12 | 0 | NNNN | NNNN |
| L13L21 | 0 | NNNN | NNNN |
| L22L23 | 0 | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN |

## NCDT

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN |
| L11L12 | 0 | NNNN | NNNN |
| L13L21 | 0 | NNNN | NNNN |
| L22L23 | 0 | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN |
| RFC | 0 | NNNN | NNNN |
| GFC | 0 | NNNN | NNNN |
| BFC | 0 | NNNN | NNNN |

## NCS

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| L11L12 | 2 | OXNN | ONNN |
| L13L21 | 1 | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN |

## NCCS

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN |
| L11L12 | 1 | ONNN | ONNN |
| L13L21 | 1 | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN |

## NCDS

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN |
| L11L12 | 2 | OXNN | ONNN |
| L13L21 | 1 | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN |
| RFC | 0 | NNNN | NNNN |
| GFC | 0 | NNNN | NNNN |
| BFC | 0 | NNNN | NNNN |

## RTPS

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 0 | NNNN | NNNN |
| VZ0 | 0 | NNNN | NNNN |
| R11R12 | 0 | NNNN | NNNN |
| R13R21 | 0 | NNNN | NNNN |
| R22R23 | 0 | NNNN | NNNN |
| R31R32 | 2 | OXNN | ONNN |
| R33 | 0 | NNNN | NNNN |
| TRX | 0 | NNNN | NNNN |
| TRY | 0 | NNNN | NNNN |
| TRZ | 2 | OONN | ONNN |
| OFX | 0 | NNNN | NNNN |
| OFY | 0 | NNNN | NNNN |
| H | 0 | NNNN | NNNN |
| DQA | 0 | NNNN | NNNN |
| DQB | 0 | NNNN | NNNN |

## RTPT

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 1 | XNNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN |
| R11R12 | 1 | XNNN | XNNN |
| R13R21 | 0 | NNNN | NNNN |
| R22R23 | 0 | NNNN | NNNN |
| R31R32 | 2 | XXNN | XNNN |
| R33 | 0 | NNNN | NNNN |
| TRX | 1 | XNNN | XNNN |
| TRY | 0 | NNNN | NNNN |
| TRZ | 2 | XXNN | XNNN |
| OFX | 0 | NNNN | NNNN |
| OFY | 0 | NNNN | NNNN |
| H | 0 | NNNN | NNNN |
| DQA | 0 | NNNN | NNNN |
| DQB | 0 | NNNN | NNNN |

## SQR

| register | min N | cached | uncached |
|---|---|---|---|
| IR1 | 2 | OONN | ONNN |
| IR2 | 1 | ONNN | ONNN |
| IR3 | 0 | NNNN | NNNN |

## OP

| register | min N | cached | uncached |
|---|---|---|---|
| IR1 | 0 | NNNN | NNNN |
| IR2 | 1 | ONNN | ONNN |
| IR3 | 2 | XXNN | XNNN |
| R11R12 | 1 | XNNN | XNNN |
| R22R23 | 2 | XXNN | XNNN |
| R33 | 1 | XNNN | XNNN |

## NCLIP

| register | min N | cached | uncached |
|---|---|---|---|
| SXY0 | 2 | XXNN | XNNN |
| SXY1 | 1 | XNNN | XNNN |
| SXY2 | 2 | XXNN | XNNN |

## DPCS

| register | min N | cached | uncached |
|---|---|---|---|
| RGBC | 2 | XXNN | XNNN |
| IR0 | 0 | NNNN | NNNN |
| RFC | 2 | OONN | ONNN |
| GFC | 1 | ONNN | ONNN |
| BFC | 0 | NNNN | NNNN |

## DPCT

| register | min N | cached | uncached |
|---|---|---|---|
| RGB0 | 2 | XXNN | XNNN |
| RGB1 | 0 | NNNN | NNNN |
| RGB2 | 0 | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN |
| RFC | 2 | XXNN | XNNN |
| GFC | 1 | XNNN | XNNN |
| BFC | 0 | NNNN | NNNN |

## DCPL

| register | min N | cached | uncached |
|---|---|---|---|
| RGBC | 2 | XXNN | XNNN |
| IR1 | 2 | OXNN | ONNN |
| IR2 | 1 | XNNN | XNNN |
| IR3 | 0 | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN |
| RFC | 2 | OONN | ONNN |
| GFC | 1 | ONNN | ONNN |
| BFC | 0 | NNNN | NNNN |

## INTPL

| register | min N | cached | uncached |
|---|---|---|---|
| IR1 | 2 | OXNN | ONNN |
| IR2 | 1 | XNNN | XNNN |
| IR3 | 0 | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN |
| RFC | 2 | OONN | ONNN |
| GFC | 1 | ONNN | ONNN |
| BFC | 0 | NNNN | NNNN |

## CC

| register | min N | cached | uncached |
|---|---|---|---|
| RGBC | 0 | NNNN | NNNN |
| IR1 | 2 | OONN | ONNN |
| IR2 | 0 | NNNN | nNNN |
| IR3 | 0 | NNNN | NNNN |
| LR1LR2 | 2 | OXNN | ONNN |
| LR3LG1 | 1 | XNNN | XNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 2 | OONN | ONNN |
| GBK | 1 | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN |

## CDP

| register | min N | cached | uncached |
|---|---|---|---|
| RGBC | 0 | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN |
| IR1 | 2 | OONN | ONNN |
| IR2 | 0 | NNNN | NNNN |
| IR3 | 0 | NNNN | NNNN |
| LR1LR2 | 2 | OXNN | ONNN |
| LR3LG1 | 1 | XNNN | XNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 2 | OONN | ONNN |
| GBK | 1 | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN |
| RFC | 0 | NNNN | NNNN |
| GFC | 0 | NNNN | NNNN |
| BFC | 0 | NNNN | NNNN |

## MVMVA(RT,V0,TR)

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| R11R12 | 2 | OXNN | ONNN |
| R13R21 | 1 | XNNN | XNNN |
| R22R23 | 0 | NNNN | NNNN |
| R31R32 | 0 | NNNN | NNNN |
| R33 | 0 | NNNN | NNNN |
| TRX | 2 | OONN | ONNN |
| TRY | 1 | ONNN | ONNN |
| TRZ | 0 | NNNN | NNNN |

## MVMVA(LL,V0,BK)

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| L11L12 | 2 | OXNN | ONNN |
| L13L21 | 1 | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN |
| RBK | 2 | OONN | ONNN |
| GBK | 1 | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN |

## MVMVA(LC,IR,BK)

| register | min N | cached | uncached |
|---|---|---|---|
| IR1 | 2 | OONN | ONNN |
| IR2 | 0 | NNNN | NNNN |
| IR3 | 0 | NNNN | NNNN |
| LR1LR2 | 2 | OXNN | ONNN |
| LR3LG1 | 1 | XNNN | XNNN |
| LG2LG3 | 0 | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN |
| RBK | 2 | OONN | ONNN |
| GBK | 1 | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN |

## MVMVA(RT,V0,NONE,lm0)

| register | min N | cached | uncached |
|---|---|---|---|
| VXY0 | 2 | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN |
| R11R12 | 2 | OXNN | ONNN |
| R13R21 | 1 | XNNN | XNNN |
| R22R23 | 0 | NNNN | NNNN |
| R31R32 | 0 | NNNN | NNNN |
| R33 | 0 | NNNN | NNNN |

## AVSZ3

| register | min N | cached | uncached |
|---|---|---|---|
| SZ1 | 2 | OONN | ONNN |
| SZ2 | 1 | ONNN | ONNN |
| SZ3 | 0 | NNNN | NNNN |
| ZSF3 | 2 | XXNN | XNNN |

## AVSZ4

| register | min N | cached | uncached |
|---|---|---|---|
| SZ0 | 0 | NNNN | NNNN |
| SZ1 | 2 | OONN | ONNN |
| SZ2 | 1 | ONNN | ONNN |
| SZ3 | 0 | NNNN | NNNN |
| ZSF4 | 2 | XXNN | XNNN |

## GPF

| register | min N | cached | uncached |
|---|---|---|---|
| IR0 | 2 | XXNN | XNNN |
| IR1 | 2 | OONN | ONNN |
| IR2 | 1 | ONNN | ONNN |
| IR3 | 0 | NNNN | NNNN |

## GPL

| register | min N | cached | uncached |
|---|---|---|---|
| IR0 | 2 | XXNN | XNNN |
| IR1 | 2 | OONN | ONNN |
| IR2 | 1 | ONNN | ONNN |
| IR3 | 0 | NNNN | NNNN |
| MAC1 | 2 | OONN | ONNN |
| MAC2 | 1 | ONNN | ONNN |
| MAC3 | 0 | NNNN | NNNN |

## Summary

min N over 271 cells, cached: 0: 185, 1: 34, 2: 52.
Uncached: 0: 185, 1: 86.
