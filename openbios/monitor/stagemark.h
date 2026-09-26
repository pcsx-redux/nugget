/* Bring-up bisect scaffolding: stage word at 0x1f800000, reached-bitmask at
   0x1f800004 (scratchpad survives a card reset; read back over DECI). */
#pragma once
#include <stdint.h>
#ifdef OPENBIOS_H2X00_MONITOR
#define STAGE_MARK(n)                                                    \
    do {                                                                 \
        *(volatile uint32_t *)0x1f800000 = 0xC0DE0000u | (n);            \
        *(volatile uint32_t *)0x1f800004 |= 1u << ((n) - 1);             \
    } while (0)
#else
#define STAGE_MARK(n) do { } while (0)
#endif
