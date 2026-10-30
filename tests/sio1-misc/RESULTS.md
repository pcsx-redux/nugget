# sio1-misc results

SCPH-1001, SCPH-5501 and SCPH-7001 give identical readings on every case
(112 of 112 pass on each).

The access width does not matter, only the address. A store drives the bus
with the source register shifted left by eight times the low two address
bits, and the halfword register at that address with bit 0 cleared latches its
own 16 bits of the bus:

| store | 5Ch gets | 5Eh gets |
|---|---|---|
| `sb`/`sh`/`sw` at 5Ch | bits 15-0 of the source | unchanged |
| `sb` at 5Dh | bits 7-0 of the source as its high byte, 00h as its low byte | unchanged |
| `sb`/`sh` at 5Eh | unchanged | bits 15-0 of the source |
| `sb` at 5Fh | unchanged | bits 7-0 of the source as its high byte, 00h as its low byte |

A byte store is never dropped and never masked to its lane, and a 32-bit
store at 5Ch does not reach 5Eh. 5Ch stores what it latches byte-swapped, with
each byte trimmed to the character length in SIO1 mode bits 2-3 (5 bits:
`(b & 1Fh) | C0h`, 6 bits: `(b & 3Fh) | 80h`, 7 bits: `b & 7Fh`, 8 bits:
unchanged). 5Eh (baud reload) stores it as is. Changing the character length
afterwards does not change the stored value. Mode reads back unchanged.

Reads: `lw` at 5Ch returns 5Eh in the upper half and 5Ch in the lower;
`lhu` at 5Ch and 5Eh and `lbu` at 5Ch-5Fh return the matching part of that
word.

Raw runs: farm tickets on SCPH-1001 and SCPH-5501 with a probe printing
every reading, then this test on all three revisions.
