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

// clang-format off

// Forward and Backward only work while playing audio. Anywhere else the drive
// answers INT5 with stat|1 and error code 0x80.

CESTER_TEST(cdlForwardWhileIdle, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    initializeTime();

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_FORWARD;

    uint32_t errorTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t response1[16];
    uint8_t responseSize1 = readResponse(response1);
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;

    cester_assert_uint_eq(5, cause1);
    cester_assert_uint_eq(0xe0, cause1b);
    cester_assert_uint_eq(3, response1[0]);
    cester_assert_uint_eq(0x80, response1[1]);
    cester_assert_uint_eq(2, responseSize1);
    ramsyscall_printf("Forward while idle: errored in %ius\n", errorTime);
)

CESTER_TEST(cdlBackwardWhileIdle, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    initializeTime();

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_BACKWARD;

    uint32_t errorTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t response1[16];
    uint8_t responseSize1 = readResponse(response1);
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;

    cester_assert_uint_eq(5, cause1);
    cester_assert_uint_eq(0xe0, cause1b);
    cester_assert_uint_eq(3, response1[0]);
    cester_assert_uint_eq(0x80, response1[1]);
    cester_assert_uint_eq(2, responseSize1);
    ramsyscall_printf("Backward while idle: errored in %ius\n", errorTime);
)
