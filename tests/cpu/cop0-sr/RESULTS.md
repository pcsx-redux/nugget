# cop0-sr results

SCPH-1000 (BIOS 2.0), SCPH-1001 (2.1), SCPH-5501 (3.0), SCPH-7001 (4.1) and
SCPH-9002 (4.1) agree on every row.
The cop0 user-mode arms other than mfc0, cfc0/ctc0, lwc0/swc0, the cop2
arms past mfc2 and the KSEG1/KSEG2 and store arms were run on SCPH-1001 and
SCPH-7001 only, which agree.

| Bit | Name | Write 1, read | Observed |
|---|---|---|---|
| 0-5 | IEc..KUo | 1 | KUc=1 is user mode: loads from and fetches in KSEG0/1/2 raise AdEL, stores raise AdES, BadA = the address |
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
| 28 | CU0 | 1 | clear: mfc0, mtc0 and rfe trap in user mode (CE=0); lwc0/swc0 trap in kernel mode too; set: no trap |
| 29, 31 | CU1, CU3 | 1 | clear: every cop1/cop3 opcode traps (ExcCode 0Bh, CE=1/3), kernel mode included; set: no trap, reads return garbage |
| 30 | CU2 | 1 | clear: every cop2 opcode traps with CE=2 (mfc2, mtc2, cfc2, ctc2, lwc2, swc2, commands) |

cfc0 and ctc0 raise Reserved Instruction (ExcCode 0Ah) in either mode, with
CU0 clear or set.

No bit reads 1 after writing 0. SR is 40000401h at entry under the loader
used. An isolated load returns the line's data whether or not the tag
matches.
