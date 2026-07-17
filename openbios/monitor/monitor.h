/*

MIT License

Copyright (c) 2026 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <stdint.h>

/* H2x00 resident debug monitor. Opcodes, error codes, and event reasons follow
   the monitor protocol design (sections 3, 6, 9). The high nibble of a TYPE
   encodes direction: 0x0_/0x2_ host-originated, 0x4_ PS1 response, 0x8_ PS1
   async event. */

/* Host -> PS1 commands */
#define MON_PING 0x01
#define MON_READ_MEM 0x02
#define MON_WRITE_MEM 0x03
#define MON_GET_REGS 0x04
#define MON_SET_REG 0x05
#define MON_SET_BP 0x06
#define MON_CLR_BP 0x07
#define MON_LOAD 0x08
#define MON_RUN 0x09
#define MON_CONT 0x0A
#define MON_STOP 0x0B
#define MON_STEP 0x0C       /* reserved / deferred */
#define MON_PCDRV_RESP 0x20 /* answering an in-flight PCDRV_REQ */

/* PS1 -> host responses / events */
#define MON_ACK 0x40
#define MON_DATA 0x41
#define MON_REGS 0x42
#define MON_PONG 0x43
#define MON_ERROR 0x4F
#define MON_HELLO 0x80
#define MON_STOPPED 0x81
#define MON_PCDRV_REQ 0x82

/* ERROR payload codes (section 9) */
#define MON_EBADCMD 0x01
#define MON_EBADSTATE 0x02
#define MON_EBADADDR 0x03
#define MON_EBADREG 0x04
#define MON_EBADLEN 0x05
#define MON_ECKSUM 0x06
#define MON_ENOFD 0x07

/* STOPPED reason codes (section 6) */
#define MON_STOP_BREAKPOINT 0x01
#define MON_STOP_INTERRUPT 0x02
#define MON_STOP_DATA_WATCH 0x03
#define MON_STOP_FAULT 0x04
#define MON_STOP_EXIT 0x05

#define MON_PROTO_VER 0x0001

/* Resident SRAM base (EXP3) and the RAM size announced in HELLO. */
#define MON_SRAM_BASE 0x1fa00000u
/* Devkit main RAM. TODO(bring-up): retail reports 2 MiB; the H2700 devkit
   carries more (8 MiB). Announce the conservative retail value until the size
   is probed for real. */
#define MON_RAM_SIZE 0x00200000u

/* Monitor entry. Never returns: it takes over from boot()'s terminal path,
   brings up the transport, announces HELLO, and runs the command loop. */
void monitorMain(void) __attribute__((noreturn));
