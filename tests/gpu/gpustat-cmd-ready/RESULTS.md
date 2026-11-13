# gpustat26 results

GPUSTAT read after each word, then polled up to 20000 times for bit 26.
"low" = bit 26 did not come back within the cap, "set" = it did (poll count
in brackets). Same table on SCPH-1000 (v0 GPU) and SCPH-5501 (v2 GPU), with
GP1(04h)=0 and GP1(04h)=2.

| command | words | after each word |
|---|---|---|
| GP0(00h) | 1 | set (0) |
| GP0(E1h) | 1 | set (0) |
| GP0(02h) fill | 3 | low, low, set (2) |
| GP0(A0h) 2x2 | 5 | low, low, low, low, set (0-1) |
| GP0(C0h) 2x2 | 3 + 2 reads | low, low, low (bit 27 set), low after read 1, set after read 2 |
| GP0(80h) 4x4 | 4 | low, low, low, set (5-8) |
| GP0(20h) | 4 | low x3, set (7-9) |
| GP0(28h) | 5 | low x4, set (7-9) |
| GP0(30h) | 6 | low x5, set (21-23) |
| GP0(24h) | 7 | low x6, set (18-20) |
| GP0(60h) | 3 | low, low, set (3) |
| GP0(68h) | 2 | low, set (1-2) |
| GP0(40h) | 3 | low, low, set (7-8) |
| GP0(48h) + 55555555h | 5 | low x4, set (0) after the terminator |
| GP0(58h) + 55555555h | 5 | low x4, set (0) after the terminator |

A C0h readout left after one of its two words, then GP0(E1h) written:
bit 26 stays low, the second GPUREAD still returns the pixel data, and bit
26 is set after it (SCPH-5501, both GP1(04h) modes).

Also in the logs, not asserted: the first GPUSTAT read right after any GP0
write has bit 26 clear, single-word commands included. While a command waits
for words, bit 28 is set for 02h, A0h, C0h, 80h, 60h and 68h and clear for
polygons and lines.

Tickets: ticket_304fcc0b-f3e1-4c98-b058-749c96582b1d (SCPH-1000),
ticket_a626ad0d-ada7-4cfa-8d0c-76f4d24d8f21 (SCPH-5501),
ticket_9dc59351-1f87-4306-ba03-3208b2e851b4 (SCPH-5501, readout arm),
ticket_76907a64-ea4a-4351-b3fc-e8e676b2ec50 (SCPH-1000, final build, readout
arm asserted, 2/2 pass).
