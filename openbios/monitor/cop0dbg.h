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

/* cop0 accessors for the debug monitor. The LR33300/LR33310 R3000A exposes a
   hardware-breakpoint unit through cop0 debug registers (BPC/BDA/DCIC/BPCM/
   BDAM); psx-spx (cpuspecifications.md) documents the layout used here. Only
   the registers the monitor needs are wrapped. */

/* -- read-only exception state -- */
static __inline__ uint32_t readBadVaddr(void) {
    uint32_t v;
    __asm__ volatile("mfc0 %0, $8\nnop\n" : "=r"(v));
    return v;
}

static __inline__ uint32_t readCause(void) {
    uint32_t v;
    __asm__ volatile("mfc0 %0, $13\nnop\n" : "=r"(v));
    return v;
}

static __inline__ uint32_t readEPC(void) {
    uint32_t v;
    __asm__ volatile("mfc0 %0, $14\nnop\n" : "=r"(v));
    return v;
}

/* -- hardware breakpoint unit -- */
/* BPC   cop0r3  : program-counter breakpoint address
   BDA   cop0r5  : data-access breakpoint address
   DCIC  cop0r7  : debug control / cause
   BDAM  cop0r9  : data-access breakpoint address mask
   BPCM  cop0r11 : program-counter breakpoint address mask */

static __inline__ void writeBPC(uint32_t v) { __asm__ volatile("mtc0 %0, $3\nnop\n" : : "r"(v)); }
static __inline__ void writeBDA(uint32_t v) { __asm__ volatile("mtc0 %0, $5\nnop\n" : : "r"(v)); }
static __inline__ void writeBPCM(uint32_t v) { __asm__ volatile("mtc0 %0, $11\nnop\n" : : "r"(v)); }
static __inline__ void writeBDAM(uint32_t v) { __asm__ volatile("mtc0 %0, $9\nnop\n" : : "r"(v)); }

static __inline__ uint32_t readDCIC(void) {
    uint32_t v;
    __asm__ volatile("mfc0 %0, $7\nnop\n" : "=r"(v));
    return v;
}
static __inline__ void writeDCIC(uint32_t v) { __asm__ volatile("mtc0 %0, $7\nnop\n" : : "r"(v)); }

/* DCIC bit fields (psx-spx cpuspecifications.md). Without TR a match only sets
   the status bits (0..4) instead of trapping to the cop0-break vector. */
#define DCIC_DE (1u << 23)  /* master debug enable */
#define DCIC_PCE (1u << 24) /* program counter breakpoint enable */
#define DCIC_DAE (1u << 25) /* data address breakpoint enable */
#define DCIC_DR (1u << 26)  /* break on data read */
#define DCIC_DW (1u << 27)  /* break on data write */
#define DCIC_KD (1u << 29)  /* enable in kernel mode */
#define DCIC_UD (1u << 30)  /* enable in user mode */
#define DCIC_TR (1u << 31)  /* trap enable (raise the exception) */

/* Cause register ExcCode field (bits 6:2). */
#define CAUSE_EXCCODE(cause) (((cause) >> 2) & 0x1f)
#define EXCCODE_INT 0x00  /* interrupt */
#define EXCCODE_ADEL 0x04 /* address error, load */
#define EXCCODE_ADES 0x05 /* address error, store */
#define EXCCODE_IBE 0x06  /* bus error, instruction fetch */
#define EXCCODE_DBE 0x07  /* bus error, data */
#define EXCCODE_SYS 0x08  /* syscall */
#define EXCCODE_BP 0x09   /* breakpoint (break instruction) */
#define EXCCODE_RI 0x0a   /* reserved instruction */
#define EXCCODE_CPU 0x0b  /* coprocessor unusable */
#define EXCCODE_OVF 0x0c  /* arithmetic overflow */
