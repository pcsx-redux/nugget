# gte-write-to-use results

SCPH-1000, SCPH-1001, SCPH-5501 (two consoles), SCPH-7001 and SCPH-9002, build 48faf1f9.

Each cell writes a register, waits N nops (N = 0..3) and runs the command. Letters, one per N:
O / N the outputs match the old / new value, o / n the same with the written register left out
(the late write landed over the command's own result), X some outputs old and some new.
A0..A3 is the word of the 16-byte cache line the sequence starts at; U is uncached.
"min N" is the smallest N from which every cached result is new, at every offset on every console.

Seven letters change with the offset, all at N=1 (N at A0, the old value or X at A1..A3):
RTPT R31R32, RTPT TRZ, INTPL IR1, INTPL RFC, MVMVA(LC,IR,BK) IR1 and RBK, GPL IR1.
The SCPH-9002 swaps DPCT RGB0 and RGB1 at N=0..1 (RGB0 NN, RGB1 XX); the others give the reverse.
Otherwise every cached letter is the same on all six consoles. The uncached column is the
SCPH-5501 #3; a few uncached letters differ between consoles. The program checks every cached
letter that is the same at all four offsets on all six consoles, and passes on all six.

## NCCT

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L11L12 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L13L21 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L22L23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## NCT

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L11L12 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L13L21 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L22L23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## NCDT

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L11L12 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L13L21 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L22L23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## NCS

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L11L12 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| L13L21 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## NCCS

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L11L12 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| L13L21 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## NCDS

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RGBC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L11L12 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| L13L21 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR3LG1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## RTPS

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R11R12 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R13R21 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R22R23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R31R32 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| R33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| TRX | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| TRY | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| TRZ | 2 | OONN | OONN | OONN | OONN | ONNN |
| OFX | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| OFY | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| H | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| DQA | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| DQB | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## RTPT

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VXY2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| VZ2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R11R12 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| R13R21 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R22R23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R31R32 | 2 | XNNN | XXNN | XXNN | XXNN | XNNN |
| R33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| TRX | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| TRY | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| TRZ | 2 | XNNN | XXNN | XXNN | XXNN | XNNN |
| OFX | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| OFY | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| H | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| DQA | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| DQB | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## SQR

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| IR1 | 2 | OONN | OONN | OONN | OONN | ONNN |
| IR2 | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## OP

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| IR1 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR2 | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| IR3 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| R11R12 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| R22R23 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| R33 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |

## NCLIP

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| SXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| SXY1 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| SXY2 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |

## DPCS

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| RGBC | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| IR0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RFC | 2 | OONN | OONN | OONN | OONN | ONNN |
| GFC | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| BFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## DPCT

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| RGB0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| RGB1 | 2 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RGB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RFC | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| GFC | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| BFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## DCPL

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| RGBC | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| IR1 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| IR2 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RFC | 2 | OONN | OONN | OONN | OONN | ONNN |
| GFC | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| BFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## INTPL

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| IR1 | 2 | ONNN | OXNN | OXNN | OXNN | ONNN |
| IR2 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RFC | 2 | ONNN | OONN | OONN | OONN | ONNN |
| GFC | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| BFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## CC

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| RGBC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR1 | 2 | OONN | OONN | OONN | OONN | ONNN |
| IR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| LR3LG1 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 2 | OONN | OONN | OONN | OONN | ONNN |
| GBK | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## CDP

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| RGBC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR1 | 2 | OONN | OONN | OONN | OONN | ONNN |
| IR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LR1LR2 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| LR3LG1 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 2 | OONN | OONN | OONN | OONN | ONNN |
| GBK | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| GFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| BFC | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## MVMVA(RT,V0,TR)

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R11R12 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| R13R21 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| R22R23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R31R32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| TRX | 2 | OONN | OONN | OONN | OONN | ONNN |
| TRY | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| TRZ | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## MVMVA(LL,V0,BK)

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L11L12 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| L13L21 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| L22L23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L31L32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| L33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 2 | OONN | OONN | OONN | OONN | ONNN |
| GBK | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## MVMVA(LC,IR,BK)

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| IR1 | 2 | ONNN | OONN | OONN | OONN | ONNN |
| IR2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | nNNN |
| LR1LR2 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| LR3LG1 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| LG2LG3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB1LB2 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| LB3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| RBK | 2 | ONNN | OONN | OONN | OONN | ONNN |
| GBK | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| BBK | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## MVMVA(RT,V0,NONE,lm0)

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| VXY0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| VZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R11R12 | 2 | OXNN | OXNN | OXNN | OXNN | ONNN |
| R13R21 | 1 | XNNN | XNNN | XNNN | XNNN | XNNN |
| R22R23 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R31R32 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| R33 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## AVSZ3

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| SZ1 | 2 | OONN | OONN | OONN | OONN | ONNN |
| SZ2 | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| SZ3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| ZSF3 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |

## AVSZ4

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| SZ0 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| SZ1 | 2 | OONN | OONN | OONN | OONN | ONNN |
| SZ2 | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| SZ3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| ZSF4 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |

## GPF

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| IR0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| IR1 | 2 | OONN | OONN | OONN | OONN | ONNN |
| IR2 | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## GPL

| register | min N | A0 | A1 | A2 | A3 | U |
|---|---|---|---|---|---|---|
| IR0 | 2 | XXNN | XXNN | XXNN | XXNN | XNNN |
| IR1 | 2 | ONNN | OONN | OONN | OONN | ONNN |
| IR2 | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| IR3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |
| MAC1 | 2 | OONN | OONN | OONN | OONN | ONNN |
| MAC2 | 1 | ONNN | ONNN | ONNN | ONNN | ONNN |
| MAC3 | 0 | NNNN | NNNN | NNNN | NNNN | NNNN |

## Summary

min N over 271 cells: 0: 184, 1: 33, 2: 54.
