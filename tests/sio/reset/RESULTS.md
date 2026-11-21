# sio-reset results

SCPH-1001, measured with a longer probe built on the same register writes,
which also filled the RX FIFO and had a byte in flight. Both ports behave the
same. Writing CR with bit 6 set leaves CR, MR and BR all reading 0, bit 6
included, and they stay 0 when CR is written again. SR drops to TX ready and
TX finished (005h) plus the live DSR/CTS inputs, with the parity, overrun,
framing and IRQ flags cleared. The RX FIFO is emptied (RX ready clears, only
bytes received afterwards count) and a byte being sent is aborted. The baud
timer reads 0 on SIO0 and keeps counting down on SIO1.

This test's own output has not been captured on silicon yet.
