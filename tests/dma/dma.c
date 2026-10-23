/*

MIT License

Copyright (c) 2023 PCSX-Redux authors

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

#ifndef PCSX_TESTS
#define PCSX_TESTS 0
#endif

#if PCSX_TESTS
#define CESTER_MAYBE_TEST CESTER_SKIP_TEST
#else
#define CESTER_MAYBE_TEST CESTER_TEST
#endif

// DMA6 cases the emulator does not pass yet; flip to CESTER_TEST once it does.
#define CESTER_OTC_TEST CESTER_MAYBE_TEST

#include "common/hardware/dma.h"

#include "common/hardware/gpu.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

#include "../cop0/cester-cop0.c"

CESTER_TEST(normal_dma, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd = 0xe1000555;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x00010001;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    const uintptr_t expectedAddr = ((uintptr_t)(&cmd + 1)) & 0xffffff;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(1, bcr);
    cester_assert_uint_eq(expectedAddr, madr);
    cester_assert_uint_eq(0x00000201, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = (dicr & ~0x7f000000) | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(normal_dma_dicr_toggle, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd = 0xe1000555;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x00010001;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    unsigned count;
    unsigned timeout;
    count = 0;
    timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    const uintptr_t expectedAddr = ((uintptr_t)(&cmd + 1)) & 0xffffff;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(1, bcr);
    cester_assert_uint_eq(expectedAddr, madr);
    cester_assert_uint_eq(0x00000201, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = 0x00800000;
    dicr = DICR;
    count = 0;
    timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 32) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    cester_assert_uint_eq(1, timeout);
    cester_assert_uint_eq(0x84800000, dicr);
    DICR = 0;
    dicr = DICR;
    count = 0;
    timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 32) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    cester_assert_uint_eq(1, timeout);
    cester_assert_uint_eq(0x04000000, dicr);
    DICR = 0x00800000;
    dicr = DICR;
    count = 0;
    timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0x84800000, dicr);
    DICR = 0x00008000;
    dicr = DICR;
    count = 0;
    timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 32) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    cester_assert_uint_eq(1, timeout);
    cester_assert_uint_eq(0x84008000, dicr);
    DICR = 0x00800000;
    dicr = DICR;
    count = 0;
    timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 32) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    cester_assert_uint_eq(1, timeout);
    cester_assert_uint_eq(0x84800000, dicr);
)

CESTER_TEST(normal_dma_with_3_upper_bits, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd = 0xe1000555;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = ((uintptr_t)&cmd) | 0xe0000000;
    DMA_CTRL[DMA_GPU].BCR = 0x00010001;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    const uintptr_t expectedAddr = ((uintptr_t)(&cmd + 1)) & 0xffffff;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(1, bcr);
    cester_assert_uint_eq(expectedAddr, madr);
    cester_assert_uint_eq(0x00000201, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = (dicr & ~0x7f000000) | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(normal_dma_with_8_upper_bits, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd = 0xe1000555;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = ((uintptr_t)&cmd) | 0xff000000;
    DMA_CTRL[DMA_GPU].BCR = 0x00010001;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    const uintptr_t expectedAddr = ((uintptr_t)(&cmd + 1)) & 0xffffff;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(1, bcr);
    cester_assert_uint_eq(expectedAddr, madr);
    cester_assert_uint_eq(0x00000201, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = (dicr & ~0x7f000000) | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(normal_dma_odd_address, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd = 0xe1000555;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = ((uintptr_t)&cmd) | 1;
    DMA_CTRL[DMA_GPU].BCR = 0x00010001;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    const uintptr_t expectedAddr = ((uintptr_t)(&cmd + 1)) & 0xffffff;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(1, bcr);
    cester_assert_uint_eq(expectedAddr, madr);
    cester_assert_uint_eq(0x00000201, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = (dicr & ~0x7f000000) | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(linked_dma_3_links, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd1[2] = { 0x01000000, 0x00000000 };
    uint32_t cmd2[2] = { 0x01000000, 0x00000000 };
    uint32_t cmd3[2] = { 0x01ffffff, 0xe1000555 };
    cmd1[0] = (((uint32_t)&cmd2) & 0xffffff) | 0x01000000;
    cmd2[0] = (((uint32_t)&cmd3) & 0xffffff) | 0x01000000;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd1;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00ffffff, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = (dicr & ~0x7f000000) | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(linked_dma_3_oddlinks, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd1[2] = { 0x01000000, 0x00000000 };
    uint32_t cmd2[2] = { 0x01000000, 0x00000000 };
    uint32_t cmd3[2] = { 0x01ffffff, 0xe1000555 };
    cmd1[0] = (((uint32_t)&cmd2) & 0xffffff) | 0x01000003;
    cmd2[0] = (((uint32_t)&cmd3) & 0xffffff) | 0x01000003;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd1;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00ffffff, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = (dicr & ~0x7f000000) | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(linked_dma_ffffff_terminator, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd[2] = { 0x01ffffff, 0xe1000555 };
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00ffffff, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    DICR = (dicr & ~0x7f000000) | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(linked_dma_fffff0_terminator, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd[2] = { 0x01fffff0, 0xe1000555 };
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00fffff0, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84848000, dicr);
    dicr &= 0xffffff;
    DICR = dicr | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x80848000, dicr);
    dicr &= 0xff7fff;
    DICR = dicr;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(linked_dma_800000_terminator, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd[2] = { 0x01800000, 0xe1000555 };
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00800000, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84848000, dicr);
    dicr &= 0xffffff;
    DICR = dicr | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x80848000, dicr);
    dicr &= 0xff7fff;
    DICR = dicr;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(linked_dma_800000_terminator_dicr_toggle, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd[2] = { 0x01800000, 0xe1000555 };
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00800000, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84848000, dicr);
    DICR = 0x00008000;
    dicr = DICR;
    cester_assert_uint_eq(0x84008000, dicr);
    DICR = 0x00000000;
    dicr = DICR;
    cester_assert_uint_eq(0x04000000, dicr);
)

CESTER_TEST(linked_dma_800001_terminator, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd[2] = { 0x01800001, 0xe1000555 };
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00800001, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84848000, dicr);
    dicr &= 0xffffff;
    DICR = dicr | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x80848000, dicr);
    dicr &= 0xff7fff;
    DICR = dicr;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(linked_dma_odd_terminator, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000800;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd[2] = { 0, 0xe1000555 };
    uint32_t terminator = 0x00ffffff;
    cmd[0] = (((uintptr_t)&terminator) & 0xffffff) | 0x01000001;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x12345678;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000401;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 128) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    cester_assert_uint_eq(0, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0x555, stat);
    cester_assert_uint_eq(0x12345678, bcr);
    cester_assert_uint_eq(0x00ffffff, madr);
    cester_assert_uint_eq(0x00000401, chcr);
    cester_assert_uint_eq(0x84840000, dicr);
    dicr &= 0xffffff;
    DICR = dicr | 0x04000000;
    dicr = DICR;
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(disabled_dma, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000000;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd = 0xe1000555;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = (uintptr_t)&cmd;
    DMA_CTRL[DMA_GPU].BCR = 0x00010001;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 32) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    const uintptr_t expectedAddr = ((uintptr_t)&cmd) & 0xffffff;
    cester_assert_uint_eq(1, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0, stat);
    cester_assert_uint_eq(0x00010001, bcr);
    cester_assert_uint_eq(expectedAddr, madr);
    cester_assert_uint_eq(0x01000201, chcr);
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_TEST(disabled_dma_odd_address, dma_tests,
    sendGPUStatus(0);
    sendGPUStatus(0x04000001);
    sendGPUData(0xe1000000);
    DPCR = 0x00000000;
    DICR = 0x00840000;
    IMASK = IRQ_VBLANK | IRQ_DMA;
    uint32_t cmd = 0xe1000555;
    sendGPUStatus(0x04000002);
    while ((GPU_STATUS & 0x10000000) == 0);
    DMA_CTRL[DMA_GPU].MADR = ((uintptr_t)&cmd) | 3;
    DMA_CTRL[DMA_GPU].BCR = 0x00010001;
    DMA_CTRL[DMA_GPU].CHCR = 0x01000201;
    unsigned count = 0;
    unsigned timeout = 0;
    while (1) {
        while ((IREG & (IRQ_VBLANK | IRQ_DMA)) == 0);
        if (IREG & IRQ_DMA) break;
        IREG &= ~IRQ_VBLANK;
        if (count++ == 32) {
            timeout = 1;
            break;
        }
    }
    IREG = 0;
    uint32_t stat = GPU_STATUS & 0x000007ff;
    uintptr_t bcr = DMA_CTRL[DMA_GPU].BCR;
    uint32_t madr = DMA_CTRL[DMA_GPU].MADR;
    uint32_t chcr = DMA_CTRL[DMA_GPU].CHCR;
    uint32_t dicr = DICR;
    const uintptr_t expectedAddr = (((uintptr_t)&cmd) & 0xffffff) | 3;
    cester_assert_uint_eq(1, timeout);
    cester_assert_uint_eq(0, s_got40);
    cester_assert_uint_eq(0, s_got80);
    cester_assert_uint_eq(0, s_from);
    cester_assert_uint_eq(0, s_epc);
    cester_assert_uint_eq(0, stat);
    cester_assert_uint_eq(0x00010001, bcr);
    cester_assert_uint_eq(expectedAddr, madr);
    cester_assert_uint_eq(0x01000201, chcr);
    cester_assert_uint_eq(0x00840000, dicr);
)

CESTER_BODY(
    struct OtcResult {
        uint32_t written, before, timeout, chcr, madr, bcr, dicr, good, term, guardLo, guardHi;
    };
    static uint32_t s_otcBuf[10];
    static void otcRun(uint32_t dpcrBefore, uint32_t chcrWrite, uint32_t dpcrAfter, struct OtcResult *r) {
        for (unsigned i = 0; i < 10; i++) s_otcBuf[i] = 0xdeadbeef;
        DPCR = dpcrBefore;
        DICR = 0x00c00000;
        IREG = 0;
        DMA_CTRL[DMA_GPUOTC].CHCR = 0;
        DMA_CTRL[DMA_GPUOTC].MADR = (uintptr_t)&s_otcBuf[8];
        DMA_CTRL[DMA_GPUOTC].BCR = 8;
        DMA_CTRL[DMA_GPUOTC].CHCR = chcrWrite;
        r->written = chcrWrite;
        r->before = DMA_CTRL[DMA_GPUOTC].CHCR;
        if (dpcrAfter != dpcrBefore) DPCR = dpcrAfter;
        unsigned count = 0;
        r->timeout = 0;
        while (DMA_CTRL[DMA_GPUOTC].CHCR & 0x01000000) {
            if (count++ == 100000) {
                r->timeout = 1;
                break;
            }
        }
        r->chcr = DMA_CTRL[DMA_GPUOTC].CHCR;
        r->madr = DMA_CTRL[DMA_GPUOTC].MADR;
        r->bcr = DMA_CTRL[DMA_GPUOTC].BCR;
        r->dicr = DICR;
        DMA_CTRL[DMA_GPUOTC].CHCR = 0;
        DICR = 0x7f000000;
        DICR = 0;
        IREG = 0;
        r->good = 0;
        for (unsigned i = 2; i <= 8; i++) {
            if (s_otcBuf[i] == (((uintptr_t)&s_otcBuf[i - 1]) & 0xffffff)) r->good++;
        }
        r->term = s_otcBuf[1];
        r->guardLo = s_otcBuf[0];
        r->guardHi = s_otcBuf[9];
        ramsyscall_printf("OTC write=%08x before=%08x timeout=%u chcr=%08x madr=%08x bcr=%08x dicr=%08x good=%u term=%08x guards=%08x,%08x\n",
                          r->written, r->before, r->timeout, r->chcr, r->madr, r->bcr, r->dicr, r->good, r->term,
                          r->guardLo, r->guardHi);
    }
)

CESTER_OTC_TEST(otc_chcr_readback, dma_tests,
    DPCR = 0;
    DMA_CTRL[DMA_GPUOTC].CHCR = 0xffffffff;
    uint32_t ones = DMA_CTRL[DMA_GPUOTC].CHCR;
    DMA_CTRL[DMA_GPUOTC].CHCR = 0xeeffffff;
    uint32_t onesNoStart = DMA_CTRL[DMA_GPUOTC].CHCR;
    DMA_CTRL[DMA_GPUOTC].CHCR = 0;
    uint32_t zero = DMA_CTRL[DMA_GPUOTC].CHCR;
    ramsyscall_printf("OTC readback ffffffff->%08x eeffffff->%08x 0->%08x\n", ones, onesNoStart, zero);
    cester_assert_uint_eq(0x51000002, ones);
    cester_assert_uint_eq(0x40000002, onesNoStart);
    cester_assert_uint_eq(0x00000002, zero);
)

CESTER_OTC_TEST(otc_start_11000002, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x11000002, 0x08000000, &r);
    cester_assert_uint_eq(0, r.timeout);
    cester_assert_uint_eq(0x00000002, r.chcr);
    const uintptr_t expectedAddr = ((uintptr_t)&s_otcBuf[8]) & 0xffffff;
    cester_assert_uint_eq(expectedAddr, r.madr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0xc0c00000, r.dicr);
    cester_assert_uint_eq(7, r.good);
    cester_assert_uint_eq(0x00ffffff, r.term);
    cester_assert_uint_eq(0xdeadbeef, r.guardLo);
    cester_assert_uint_eq(0xdeadbeef, r.guardHi);
)

CESTER_OTC_TEST(otc_start_11000000, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x11000000, 0x08000000, &r);
    cester_assert_uint_eq(0, r.timeout);
    cester_assert_uint_eq(0x00000002, r.chcr);
    const uintptr_t expectedAddr = ((uintptr_t)&s_otcBuf[8]) & 0xffffff;
    cester_assert_uint_eq(expectedAddr, r.madr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0xc0c00000, r.dicr);
    cester_assert_uint_eq(7, r.good);
    cester_assert_uint_eq(0x00ffffff, r.term);
    cester_assert_uint_eq(0xdeadbeef, r.guardLo);
    cester_assert_uint_eq(0xdeadbeef, r.guardHi);
)

CESTER_OTC_TEST(otc_start_51000f07, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x51000f07, 0x08000000, &r);
    cester_assert_uint_eq(0, r.timeout);
    cester_assert_uint_eq(0x40000002, r.chcr);
    const uintptr_t expectedAddr = ((uintptr_t)&s_otcBuf[8]) & 0xffffff;
    cester_assert_uint_eq(expectedAddr, r.madr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0xc0c00000, r.dicr);
    cester_assert_uint_eq(7, r.good);
    cester_assert_uint_eq(0x00ffffff, r.term);
    cester_assert_uint_eq(0xdeadbeef, r.guardLo);
    cester_assert_uint_eq(0xdeadbeef, r.guardHi);
)

CESTER_OTC_TEST(otc_start_11000f05, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x11000f05, 0x08000000, &r);
    cester_assert_uint_eq(0, r.timeout);
    cester_assert_uint_eq(0x00000002, r.chcr);
    const uintptr_t expectedAddr = ((uintptr_t)&s_otcBuf[8]) & 0xffffff;
    cester_assert_uint_eq(expectedAddr, r.madr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0xc0c00000, r.dicr);
    cester_assert_uint_eq(7, r.good);
    cester_assert_uint_eq(0x00ffffff, r.term);
    cester_assert_uint_eq(0xdeadbeef, r.guardLo);
    cester_assert_uint_eq(0xdeadbeef, r.guardHi);
)

CESTER_OTC_TEST(otc_start_41000002, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x41000002, 0x08000000, &r);
    cester_assert_uint_eq(1, r.timeout);
    cester_assert_uint_eq(0x41000002, r.chcr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0x00c00000, r.dicr);
    cester_assert_uint_eq(0, r.good);
    cester_assert_uint_eq(0xdeadbeef, r.term);
)

CESTER_OTC_TEST(otc_start_01000002, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x01000002, 0x08000000, &r);
    cester_assert_uint_eq(1, r.timeout);
    cester_assert_uint_eq(0x01000002, r.chcr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0x00c00000, r.dicr);
    cester_assert_uint_eq(0, r.good);
    cester_assert_uint_eq(0xdeadbeef, r.term);
)

CESTER_OTC_TEST(otc_start_01000000, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x01000000, 0x08000000, &r);
    cester_assert_uint_eq(1, r.timeout);
    cester_assert_uint_eq(0x01000002, r.chcr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0x00c00000, r.dicr);
    cester_assert_uint_eq(0, r.good);
    cester_assert_uint_eq(0xdeadbeef, r.term);
)

CESTER_TEST(otc_start_10000002, dma_tests,
    struct OtcResult r;
    otcRun(0x08000000, 0x10000002, 0x08000000, &r);
    cester_assert_uint_eq(0, r.timeout);
    cester_assert_uint_eq(0x10000002, r.chcr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0x00c00000, r.dicr);
    cester_assert_uint_eq(0, r.good);
    cester_assert_uint_eq(0xdeadbeef, r.term);
)

CESTER_TEST(otc_start_disabled, dma_tests,
    struct OtcResult r;
    otcRun(0x00000000, 0x11000002, 0x00000000, &r);
    cester_assert_uint_eq(1, r.timeout);
    cester_assert_uint_eq(0x11000002, r.chcr);
    cester_assert_uint_eq(8, r.bcr);
    cester_assert_uint_eq(0x00c00000, r.dicr);
    cester_assert_uint_eq(0, r.good);
    cester_assert_uint_eq(0xdeadbeef, r.term);
)

// Enabling the channel in DPCR starts a transfer already latched in CHCR.
CESTER_MAYBE_TEST(otc_start_disabled_then_enabled, dma_tests,
    struct OtcResult r;
    otcRun(0x00000000, 0x11000002, 0x08000000, &r);
    cester_assert_uint_eq(0, r.timeout);
    cester_assert_uint_eq(0x00000002, r.chcr);
    cester_assert_uint_eq(0xc0c00000, r.dicr);
    cester_assert_uint_eq(7, r.good);
    cester_assert_uint_eq(0x00ffffff, r.term);
)
