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

// On hardware these need someone to open the lid during waitForDoor and leave it open for
// doorOpenedOrMissingDisc. Under PCSX_TESTS the runner registers PCSX.execSlots handlers
// that open (CDROM_LID_OPEN_SLOT) and close (CDROM_LID_CLOSE_SLOT) the emulated lid.
#define CDROM_LID_OPEN_SLOT 0xd0
#define CDROM_LID_CLOSE_SLOT 0xd1

#if PCSX_TESTS
#define CESTER_LID_TEST CESTER_TEST
#else
#define CESTER_LID_TEST CESTER_SKIP_TEST
#endif

CESTER_LID_TEST(waitForDoor, test_instances,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    int seekDone = seekPTo(0x70, 0x19, 0x73);
    if (!seekDone) {
        cester_assert_true(seekDone);
        return;
    }

    ramsyscall_printf("Waiting for door to open...\n");
#if PCSX_TESTS
    pcsx_execSlot(CDROM_LID_OPEN_SLOT);
#endif

    initializeTime();
    uint32_t ackTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t ctrl1 = CDROM_REG0 & ~3;
    uint8_t response[16];
    uint8_t responseSize = readResponse(response);
    uint8_t ctrl2 = CDROM_REG0 & ~3;
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;

    cester_assert_uint_eq(5, cause1);
    cester_assert_uint_eq(0x38, ctrl1);
    cester_assert_uint_eq(0x18, ctrl2);
    cester_assert_uint_eq(0x16, response[0]);
    cester_assert_uint_eq(8, response[1]);
    cester_assert_uint_eq(2, responseSize);
)

CESTER_LID_TEST(doorOpenedOrMissingDisc, test_instances,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_GETLOCP;

    initializeTime();
    uint32_t ackTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t ctrl1 = CDROM_REG0 & ~3;
    uint8_t response[16];
    uint8_t responseSize = readResponse(response);
    uint8_t ctrl2 = CDROM_REG0 & ~3;
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;
#if PCSX_TESTS
    pcsx_execSlot(CDROM_LID_CLOSE_SLOT);
    // The shell-open status bit stays latched until a GetStat; clear it for the tests after this one.
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_NOP;
    waitCDRomIRQ();
    ackCDRomCause();
    discardResponse();
#endif

    cester_assert_uint_eq(5, cause1);
    cester_assert_uint_eq(0x38, ctrl1);
    cester_assert_uint_eq(0x18, ctrl2);
    cester_assert_uint_eq(0x11, response[0]);
    cester_assert_uint_eq(0x80, response[1]);
    cester_assert_uint_eq(2, responseSize);
)
