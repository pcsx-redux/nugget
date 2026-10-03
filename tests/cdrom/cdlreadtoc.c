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

CESTER_TEST(cdlReadTOC, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    uint8_t response[16];
    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_READTOC;
    uint32_t ackTime = 1000000;
    int ackOk = waitCDRomIRQWithTimeout(&ackTime);
    uint8_t cause1 = ackCDRomCause();
    uint8_t size1 = readResponse(response);
    uint8_t stat1 = response[0];
    uint32_t completeTime = 10000000;
    int completeOk = waitCDRomIRQWithTimeout(&completeTime);
    uint8_t cause2 = ackCDRomCause();
    uint8_t size2 = readResponse(response);
    uint8_t stat2 = response[0];

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_NOP;
    uint32_t nopTime = 1000000;
    int nopOk = waitCDRomIRQWithTimeout(&nopTime);
    ackCDRomCause();
    readResponse(response);
    uint8_t nopStat = nopOk ? response[0] : 0xff;

    ramsyscall_printf("ReadTOC: ack %i %i size %i stat %02x in %ius, complete %i %i size %i stat %02x in %ius, Nop stat %02x\n",
                      ackOk, cause1, size1, stat1, ackTime, completeOk, cause2, size2, stat2, completeTime, nopStat);
    cester_assert_true(ackOk);
    cester_assert_uint_eq(3, cause1);
    cester_assert_true(completeOk);
    cester_assert_uint_eq(0x02, stat1);
    cester_assert_uint_lt(ackTime, 10000);
    cester_assert_uint_eq(2, cause2);
    cester_assert_uint_eq(0x02, stat2);
    cester_assert_uint_ge(completeTime, 500000);
    cester_assert_uint_lt(completeTime, 1500000);
    cester_assert_true(nopOk);
    cester_assert_uint_eq(0x02, nopStat);
)
