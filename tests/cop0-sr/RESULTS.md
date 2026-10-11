# cop0-sr results

SCPH-1000 (BIOS 2.0), SCPH-1001 (2.1), SCPH-5501 (3.0), SCPH-7001 (4.1) and
SCPH-9002 (4.1) agree on every row.

| Bit | Name | Write 1, read | Observed |
|---|---|---|---|
| 0-5 | IEc..KUo | 1 | KUc=1 is user mode: mfc0 traps with CU0 clear, KSEG0 loads and fetches raise AdEL |
| 6-7 | | 0 | |
| 8-9 | IM | 1 | Cause bits 8-9 are writable and interrupt when IM and IEc are set |
| 10 | IM | 1 | Cause bit 10 follows I_STAT & I_MASK as soon as either changes |
| 11-15 | IM | 1 | Cause bits 11-15 never set |
| 16 | IsC | 1 | isolated loads read the i-cache line |
| 17 | SwC | 1 | no change |
| 18 | PZ | 1 | no change, no parity error |
| 19 | CM | 1 | holds the written value; isolated hits and misses never change it |
| 20 | PE | 0 | |
| 21 | TS | 0 | |
| 22 | BEV | 1 | a syscall stops reaching 80000080h |
| 23-24 | | 0 | |
| 25 | RE | 0 | user-mode byte and halfword accesses unchanged |
| 26-27 | | 0 | |
| 28 | CU0 | 1 | needed for cop0 access in user mode only |
| 29, 31 | CU1, CU3 | 1 | clear: every cop1/cop3 opcode traps (ExcCode 0Bh, CE=1/3), kernel mode included; set: no trap, reads return garbage |
| 30 | CU2 | 1 | clear: mfc2 traps with CE=2 |

No bit reads 1 after writing 0. SR is 40000401h at entry under the loader
used. An isolated load returns the line's data whether or not the tag
matches.
