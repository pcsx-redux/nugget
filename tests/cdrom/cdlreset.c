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

CESTER_TEST(cdlResetMode, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }
    int modeSet = setMode(0xc8);
    if (!modeSet) {
        cester_assert_true(modeSet);
        return;
    }

    uint8_t response[16];
    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_RESET;
    uint32_t ackTime = 1000000;
    int ackOk = waitCDRomIRQWithTimeout(&ackTime);
    uint8_t cause1 = ackCDRomCause();
    uint8_t size1 = readResponse(response);
    uint8_t stat1 = response[0];

    // Anything else the drive sends within 3 s.
    uint32_t extraTime = 3000000;
    int extraOk = waitCDRomIRQWithTimeout(&extraTime);
    uint8_t cause2 = extraOk ? ackCDRomCause() : 0;
    uint8_t size2 = extraOk ? readResponse(response) : 0;
    uint8_t stat2 = extraOk ? response[0] : 0;

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_GETMODE;
    uint32_t paramTime = 6000000;
    int paramOk = waitCDRomIRQWithTimeout(&paramTime);
    uint8_t cause3 = paramOk ? ackCDRomCause() : 0;
    uint8_t size3 = paramOk ? readResponse(response) : 0;

    ramsyscall_printf("Reset: ack %i cause %i size %i stat %02x in %ius; extra %i cause %i size %i stat %02x at %ius; GetParam %i cause %i size %i: %02x %02x %02x %02x %02x at %ius\n",
                      ackOk, cause1, size1, stat1, ackTime, extraOk, cause2, size2, stat2, extraTime, paramOk, cause3,
                      size3, response[0], response[1], response[2], response[3], response[4], paramTime);
    uint8_t mode = response[1];
    uint8_t paramStat = response[0];

    // The shell-open bit stays set until a GetStat, which clears it.
    uint8_t nopStat[2];
    for (unsigned i = 0; i < 2; i++) {
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_NOP;
        uint32_t nopTime = 1000000;
        int nopOk = waitCDRomIRQWithTimeout(&nopTime);
        ackCDRomCause();
        readResponse(response);
        nopStat[i] = nopOk ? response[0] : 0xff;
    }
    ramsyscall_printf("Reset: Nop stats %02x %02x\n", nopStat[0], nopStat[1]);

    cester_assert_true(ackOk);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(0x02, stat1);
    cester_assert_uint_lt(ackTime, 10000);
    cester_assert_false(extraOk);
    cester_assert_true(paramOk);
    cester_assert_uint_eq(3, cause3);
    cester_assert_uint_eq(0x12, paramStat);
    cester_assert_uint_eq(0x20, mode);
    cester_assert_uint_eq(0x12, nopStat[0]);
    cester_assert_uint_eq(0x02, nopStat[1]);
)
