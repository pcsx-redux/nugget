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
    // Sends a command with no parameters and records its two responses.
    // Returns 0 if either response does not arrive within its timeout.
    static int stopCommand(uint8_t command, uint8_t* cause1, uint8_t* stat1, uint32_t* ackTime, uint8_t* cause2,
                           uint8_t* stat2, uint32_t* completeTime) {
        uint8_t response[16];
        *cause1 = *stat1 = *cause2 = *stat2 = 0xff;
        *ackTime = *completeTime = 0;
        initializeTime();
        CDROM_REG0 = 0;
        CDROM_REG1 = command;
        uint32_t timeout = 1000000;
        if (!waitCDRomIRQWithTimeout(&timeout)) return 0;
        *ackTime = timeout;
        *cause1 = ackCDRomCause();
        readResponse(response);
        *stat1 = response[0];
        timeout = 10000000;
        if (!waitCDRomIRQWithTimeout(&timeout)) return 0;
        *completeTime = timeout;
        *cause2 = ackCDRomCause();
        readResponse(response);
        *stat2 = response[0];
        return 1;
    }

    static uint8_t stopNopStat() {
        uint8_t response[16];
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_NOP;
        waitCDRomIRQ();
        ackCDRomCause();
        readResponse(response);
        return response[0];
    }
)

CESTER_TEST(cdlStopSpinning, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    uint8_t cause1, stat1, cause2, stat2;
    uint32_t ackTime, completeTime;
    int ok = stopCommand(CDL_STOP, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    uint8_t nopStat = stopNopStat();
    ramsyscall_printf("Stop while spinning: ack %i stat %02x in %ius, complete %i stat %02x in %ius, Nop stat %02x\n",
                      cause1, stat1, ackTime, cause2, stat2, completeTime, nopStat);
    cester_assert_true(ok);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(0x02, stat1);
    cester_assert_uint_lt(ackTime, 10000);
    cester_assert_uint_eq(2, cause2);
    cester_assert_uint_eq(0x00, stat2);
    cester_assert_uint_ge(completeTime, 300000);
    cester_assert_uint_lt(completeTime, 600000);
    cester_assert_uint_eq(0x00, nopStat);
)

CESTER_TEST(cdlStopStopped, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    uint8_t cause1, stat1, cause2, stat2;
    uint32_t ackTime, completeTime;
    int setupOk = stopCommand(CDL_STOP, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    if (!setupOk) {
        cester_assert_true(setupOk);
        return;
    }
    int ok = stopCommand(CDL_STOP, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    ramsyscall_printf("Stop while stopped: ack %i stat %02x in %ius, complete %i stat %02x in %ius\n", cause1, stat1,
                      ackTime, cause2, stat2, completeTime);
    cester_assert_true(ok);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(0x00, stat1);
    cester_assert_uint_eq(2, cause2);
    cester_assert_uint_eq(0x00, stat2);
    cester_assert_uint_lt(completeTime, 10000);
)

CESTER_TEST(cdlStandbyAfterStop, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    uint8_t cause1, stat1, cause2, stat2;
    uint32_t ackTime, completeTime;
    int setupOk = stopCommand(CDL_STOP, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    if (!setupOk) {
        cester_assert_true(setupOk);
        return;
    }
    int ok = stopCommand(CDL_STANDBY, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    uint8_t nopStat = stopNopStat();
    ramsyscall_printf("Standby after Stop: ack %i stat %02x in %ius, complete %i stat %02x in %ius, Nop stat %02x\n",
                      cause1, stat1, ackTime, cause2, stat2, completeTime, nopStat);
    cester_assert_true(ok);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(0x00, stat1);
    cester_assert_uint_eq(2, cause2);
    cester_assert_uint_eq(0x02, stat2);
    cester_assert_uint_ge(completeTime, 1500000);
    cester_assert_uint_lt(completeTime, 4000000);
    cester_assert_uint_eq(0x02, nopStat);
)

CESTER_TEST(cdlStandbySpinning, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    uint8_t response[16];
    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_STANDBY;
    uint32_t timeout = 1000000;
    int ok = waitCDRomIRQWithTimeout(&timeout);
    uint8_t cause = ackCDRomCause();
    uint8_t size = readResponse(response);
    ramsyscall_printf("Standby while spinning: ok %i cause %i size %i resp %02x %02x in %ius\n", ok, cause, size,
                      response[0], response[1], timeout);
    cester_assert_true(ok);
    cester_assert_uint_eq(5, cause);
    cester_assert_uint_eq(2, size);
    cester_assert_uint_eq(0x03, response[0]);
    cester_assert_uint_eq(0x20, response[1]);
)
