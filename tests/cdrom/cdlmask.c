/*

MIT License

Copyright (c) 2022 PCSX-Redux authors

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

// clang-format off

CESTER_BODY(
    // Masks the interrupt causes with HINTMSK, sends Nop, and looks at what is pending
    // while masked and after unmasking.
    static void maskedNop(uint8_t mask, uint8_t* irqWhileMasked, uint8_t* stsWhileMasked, uint8_t* ctrlWhileMasked,
                          uint8_t* irqAfterUnmask, uint8_t* stsAfterUnmask, uint8_t* size, uint8_t* stat) {
        uint8_t response[16];
        CDROM_REG0 = 1;
        CDROM_REG2 = mask;
        IREG &= ~IRQ_CDROM;
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_NOP;
        initializeTime();
        while (updateTime() < 20000);
        *irqWhileMasked = (IREG & IRQ_CDROM) != 0;
        *ctrlWhileMasked = CDROM_REG0 & ~3;
        CDROM_REG0 = 1;
        *stsWhileMasked = CDROM_REG3_UC;
        CDROM_REG0 = 1;
        CDROM_REG2 = 0x1f;
        initializeTime();
        while (updateTime() < 1000);
        *irqAfterUnmask = (IREG & IRQ_CDROM) != 0;
        IREG &= ~IRQ_CDROM;
        CDROM_REG0 = 1;
        *stsAfterUnmask = CDROM_REG3_UC;
        ackCDRomCause();
        *size = readResponse(response);
        *stat = response[0];
    }
)

CESTER_TEST(cdlMaskedCauseLatches, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    static const uint8_t masks[] = {0x00, 0x1c, 0x1b};
    for (unsigned i = 0; i < sizeof(masks); i++) {
        uint8_t irqMasked, stsMasked, ctrlMasked, irqAfter, stsAfter, size, stat;
        maskedNop(masks[i], &irqMasked, &stsMasked, &ctrlMasked, &irqAfter, &stsAfter, &size, &stat);
        ramsyscall_printf("Masked Nop, HINTMSK %02x: irq %i HINTSTS %02x HSTS %02x while masked; irq %i HINTSTS %02x after unmask; response size %i stat %02x\n",
                          masks[i], irqMasked, stsMasked, ctrlMasked, irqAfter, stsAfter, size, stat);
        // The cause lands in HINTSTS either way; HINTMSK is ANDed with the cause value to gate
        // the interrupt line, and unmasking raises it. Measured on a SCPH-7502.
        uint8_t expectIrq = (masks[i] & 3) != 0;
        cester_assert_uint_eq(expectIrq, irqMasked);
        cester_assert_uint_eq(0xe3, stsMasked);
        cester_assert_uint_eq(1, irqAfter);
        cester_assert_uint_eq(0xe3, stsAfter);
        cester_assert_uint_eq(1, size);
        cester_assert_uint_eq(0x02, stat);
    }
)
