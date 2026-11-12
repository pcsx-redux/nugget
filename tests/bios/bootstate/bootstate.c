/* Dumps the hardware and CPU state a boot executable is handed by the BIOS
   and its shell. Registers are captured in entry.s before any code runs. */

#include <stdint.h>

#include "common/syscalls/syscalls.h"

/* In .data so the BIOS loads it with the image; nothing clears it. */
uint32_t g_entryRegs[38] = {1};

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a) (*(volatile uint8_t *)(a))

static const char *const s_gprNames[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0",   "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};

static uint32_t s_hw[96];
static unsigned s_n;

static void take(uint32_t v) { s_hw[s_n++] = v; }

int main() {
    /* Read everything first, print afterwards. */
    for (unsigned a = 0x1f801000; a <= 0x1f801020; a += 4) take(R32(a));
    take(R32(0x1f801060));
    take(R32(0x1f801070));
    take(R32(0x1f801074));
    for (unsigned c = 0; c < 7; c++) {
        take(R32(0x1f801080 + c * 16));
        take(R32(0x1f801084 + c * 16));
        take(R32(0x1f801088 + c * 16));
    }
    take(R32(0x1f8010f0));
    take(R32(0x1f8010f4));
    for (unsigned t = 0; t < 3; t++) {
        take(R16(0x1f801100 + t * 16));
        take(R16(0x1f801104 + t * 16));
        take(R16(0x1f801108 + t * 16));
    }
    take(R32(0x1f801814));
    take(R16(0x1f801d80));
    take(R16(0x1f801d82));
    take(R16(0x1f801d84));
    take(R16(0x1f801d86));
    take(R16(0x1f801d98) | (R16(0x1f801d9a) << 16));
    take(R16(0x1f801d9c) | (R16(0x1f801d9e) << 16));
    take(R16(0x1f801da2));
    take(R16(0x1f801daa));
    take(R16(0x1f801dac));
    take(R16(0x1f801dae));
    take(R16(0x1f801db0));
    take(R16(0x1f801db2));
    uint8_t cdIndex = R8(0x1f801800);
    take(cdIndex);
    R8(0x1f801800) = 0;
    take(R8(0x1f801803));
    R8(0x1f801800) = 1;
    take(R8(0x1f801803));
    R8(0x1f801800) = cdIndex & 3;
    take(R32(0x1f801044));
    take(R16(0x1f801048));
    take(R16(0x1f80104a));
    take(R16(0x1f80104e));
    uint32_t gte[6] = {0};
    int haveGte = (g_entryRegs[34] >> 30) & 1;
    if (haveGte) {
        __asm__ volatile("cfc2 %0, $24" : "=r"(gte[0]));
        __asm__ volatile("cfc2 %0, $25" : "=r"(gte[1]));
        __asm__ volatile("cfc2 %0, $26" : "=r"(gte[2]));
        __asm__ volatile("cfc2 %0, $29" : "=r"(gte[3]));
        __asm__ volatile("cfc2 %0, $30" : "=r"(gte[4]));
        __asm__ volatile("cfc2 %0, $31" : "=r"(gte[5]));
    }

    static const char *const s_hwNames[] = {
        "EXP1_BASE", "EXP2_BASE", "EXP1_DELAY", "EXP3_DELAY", "BIOS_DELAY", "SPU_DELAY", "CDROM_DELAY",
        "EXP2_DELAY", "COM_DELAY", "RAM_SIZE",  "I_STAT",    "I_MASK",     "D0_MADR",    "D0_BCR",
        "D0_CHCR",   "D1_MADR",   "D1_BCR",     "D1_CHCR",   "D2_MADR",    "D2_BCR",     "D2_CHCR",
        "D3_MADR",   "D3_BCR",    "D3_CHCR",    "D4_MADR",   "D4_BCR",     "D4_CHCR",    "D5_MADR",
        "D5_BCR",    "D5_CHCR",   "D6_MADR",    "D6_BCR",    "D6_CHCR",    "DPCR",       "DICR",
        "T0_COUNT",  "T0_MODE",   "T0_TARGET",  "T1_COUNT",  "T1_MODE",    "T1_TARGET",  "T2_COUNT",
        "T2_MODE",   "T2_TARGET", "GPUSTAT",    "SPU_MVOLL", "SPU_MVOLR",  "SPU_RVOLL",  "SPU_RVOLR",
        "SPU_KOFF?", "SPU_ENDX",  "SPU_RVBADDR", "SPUCNT",   "SPU_XFERCTL", "SPUSTAT",   "SPU_CDVOLL",
        "SPU_CDVOLR", "CD_STATUS", "CD_IE",     "CD_IF",     "JOY_STAT",   "JOY_MODE",   "JOY_CTRL",
        "JOY_BAUD",
    };
    ramsyscall_printf("BOOTSTATE begin\n");
    for (unsigned i = 1; i < 32; i++) {
        if (i == 26) continue;
        ramsyscall_printf("BOOTSTATE gpr %s %08x\n", s_gprNames[i], g_entryRegs[i]);
    }
    ramsyscall_printf("BOOTSTATE gpr hi %08x\n", g_entryRegs[32]);
    ramsyscall_printf("BOOTSTATE gpr lo %08x\n", g_entryRegs[33]);
    ramsyscall_printf("BOOTSTATE cop0 SR %08x\n", g_entryRegs[34]);
    ramsyscall_printf("BOOTSTATE cop0 Cause %08x\n", g_entryRegs[35]);
    ramsyscall_printf("BOOTSTATE cop0 EPC %08x\n", g_entryRegs[36]);
    ramsyscall_printf("BOOTSTATE cop0 DCIC %08x\n", g_entryRegs[37]);
    for (unsigned i = 0; i < s_n; i++) ramsyscall_printf("BOOTSTATE hw %s %08x\n", s_hwNames[i], s_hw[i]);
    if (haveGte) {
        ramsyscall_printf("BOOTSTATE gte OFX %08x OFY %08x H %08x ZSF3 %08x ZSF4 %08x FLAG %08x\n", gte[0], gte[1],
                          gte[2], gte[3], gte[4], gte[5]);
    }
    ramsyscall_printf("BOOTSTATE count %u end\n", s_n);
    *((volatile int16_t *)0x1f802082) = 0;
    while (1);
}
