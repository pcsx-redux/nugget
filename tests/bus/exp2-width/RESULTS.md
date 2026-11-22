# exp2-width results

SCPH-1000, SCPH-1001, SCPH-5501 and SCPH-9002, identical: every read and write
completes, with DEV8 (1F80101Ch) at the value it had on entry (00080777h) and
with bit 12 set (16-bit). Reads of 1F802000h return FFh, FFFFh, FFFFFFFFh
(nothing on the bus of a retail board). No exception, no hang.
