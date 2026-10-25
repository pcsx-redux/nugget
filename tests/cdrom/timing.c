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

// Drive timings that depend on what the drive was doing before the command.
// Ranges are from SCPH-7001 and SCPH-9002 runs; a CD-R in the SCPH-9002 is
// the slow end of each.

CESTER_BODY(
    // Waits for the next cause within timeout us of the current time base,
    // acknowledges it and returns it (0 on timeout). The time lands in *time,
    // the first response byte in *stat.
    static uint8_t timingNextCause(uint32_t timeout, uint32_t* time, uint8_t* stat) {
        uint8_t response[16];
        uint32_t t = timeout;
        *stat = 0xff;
        if (!waitCDRomIRQWithTimeout(&t)) return 0;
        *time = t;
        uint8_t cause = ackCDRomCause();
        if (readResponse(response)) *stat = response[0];
        return cause;
    }

    // ReadN at 00:02:16 in the given mode, n sectors acknowledged without
    // reading them, then Pause. Returns the Pause INT2 time from its INT3,
    // or 0 if a step failed. Late INT1s around the Pause are skipped.
    static uint32_t timingPauseAfter(uint8_t mode, unsigned n, uint8_t* readStat, uint8_t* pauseStat) {
        uint32_t t;
        uint8_t stat, cause;
        if (!resetCDRom()) return 0;
        if (!setMode(mode)) return 0;
        if (!setLoc(0, 2, 0x16)) return 0;
        initializeTime();
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_READN;
        if (timingNextCause(100000, &t, &stat) != 3) return 0;
        for (unsigned i = 0; i < n; i++) {
            if (timingNextCause(3000000, &t, readStat) != 1) return 0;
        }
        initializeTime();
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_PAUSE;
        uint32_t ackTime;
        while ((cause = timingNextCause(100000, &ackTime, &stat)) == 1);
        if (cause != 3) return 0;
        uint32_t completeTime;
        while ((cause = timingNextCause(3000000, &completeTime, pauseStat)) == 1);
        if (cause != 2) return 0;
        return completeTime - ackTime;
    }
)

CESTER_TEST(cdlIdSecondResponseGap, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    uint8_t cause1, stat1, cause2, stat2;
    uint32_t ackTime, completeTime;
    int ok = stopCommand(CDL_GETID, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    ramsyscall_printf("GetID: ack in %ius, complete in %ius\n", ackTime, completeTime);
    cester_assert_true(ok);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(2, cause2);
    // The second response comes about 0.6ms after the first.
    cester_assert_uint_ge(completeTime - ackTime, 192);
    cester_assert_uint_lt(completeTime - ackTime, 2000);
)

CESTER_TEST(pauseAfterOneSector1x, test_instance,
    uint8_t readStat, pauseStat;
    uint32_t time = timingPauseAfter(0x00, 1, &readStat, &pauseStat);
    ramsyscall_printf("Pause after 1 sector @1x: complete in %ius\n", time);
    cester_assert_uint_ne(0, time);
    cester_assert_uint_eq(0x22, readStat);
    cester_assert_uint_eq(0x02, pauseStat);
    // 5 sector periods: about 64ms to 69ms, a CD-R sometimes up to 109ms.
    cester_assert_uint_ge(time, 55000);
    cester_assert_uint_lt(time, 125000);
)

CESTER_TEST(pauseAfterThreeSectors1x, test_instance,
    uint8_t readStat, pauseStat;
    uint32_t time = timingPauseAfter(0x00, 3, &readStat, &pauseStat);
    ramsyscall_printf("Pause after 3 sectors @1x: complete in %ius\n", time);
    cester_assert_uint_ne(0, time);
    cester_assert_uint_eq(0x22, readStat);
    cester_assert_uint_eq(0x02, pauseStat);
    // 8 sector periods: about 104ms to 109ms, a sector period or two either way (91ms to 135ms seen).
    cester_assert_uint_ge(time, 80000);
    cester_assert_uint_lt(time, 150000);
)

CESTER_TEST(pauseAfterOneSector2x, test_instance,
    uint8_t readStat, pauseStat;
    uint32_t time = timingPauseAfter(0x80, 1, &readStat, &pauseStat);
    ramsyscall_printf("Pause after 1 sector @2x: complete in %ius\n", time);
    cester_assert_uint_ne(0, time);
    cester_assert_uint_eq(0x22, readStat);
    cester_assert_uint_eq(0x02, pauseStat);
    // About 32ms to 35ms.
    cester_assert_uint_ge(time, 27000);
    cester_assert_uint_lt(time, 42000);
)

CESTER_TEST(pauseAfterThreeSectors2x, test_instance,
    uint8_t readStat, pauseStat;
    uint32_t time = timingPauseAfter(0x80, 3, &readStat, &pauseStat);
    ramsyscall_printf("Pause after 3 sectors @2x: complete in %ius\n", time);
    cester_assert_uint_ne(0, time);
    cester_assert_uint_eq(0x22, readStat);
    cester_assert_uint_eq(0x02, pauseStat);
    // About 45ms to 55ms.
    cester_assert_uint_ge(time, 42000);
    cester_assert_uint_lt(time, 65000);
)

CESTER_TEST(cdlInitAfterDoubleSpeedRead, test_instance,
    uint8_t readStat, pauseStat;
    uint32_t pauseTime = timingPauseAfter(0x80, 2, &readStat, &pauseStat);
    if (!pauseTime) {
        cester_assert_uint_ne(0, pauseTime);
        return;
    }

    uint8_t cause1, stat1, cause2, stat2;
    uint32_t ackTime, completeTime;
    int ok = stopCommand(CDL_INIT, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    ramsyscall_printf("Init after 2x: ack in %ius, complete in %ius\n", ackTime, completeTime);
    cester_assert_true(ok);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(2, cause2);
    cester_assert_uint_eq(0x02, stat2);
    // Dropping back to single speed after a 2x read: about 400ms to 760ms. Right after a
    // Setmode 80h with no read it was sometimes as fast as an idle Init (5ms).
    cester_assert_uint_ge(completeTime, 300000);
    cester_assert_uint_lt(completeTime, 1000000);
)

CESTER_TEST(cdlInitAfterStop, test_instance,
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
    int ok = stopCommand(CDL_INIT, &cause1, &stat1, &ackTime, &cause2, &stat2, &completeTime);
    ramsyscall_printf("Init after Stop: ack in %ius, complete in %ius\n", ackTime, completeTime);
    cester_assert_true(ok);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(2, cause2);
    cester_assert_uint_eq(0x02, stat2);
    // Spinning up: about 1.5s to 3.3s.
    cester_assert_uint_ge(completeTime, 1200000);
    cester_assert_uint_lt(completeTime, 4500000);
)

CESTER_TEST(readNAfterStop, test_instance,
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
    cester_assert_true(setLoc(0, 2, 0x16));

    uint32_t readAckTime = 0, readyTime = 0;
    uint8_t readAckStat, readyStat, pauseStat, cause;
    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_READN;
    uint8_t readCause = timingNextCause(100000, &readAckTime, &readAckStat);
    uint8_t readyCause = timingNextCause(5000000, &readyTime, &readyStat);
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_PAUSE;
    uint32_t t;
    while ((cause = timingNextCause(100000, &t, &pauseStat)) == 1);
    while ((cause = timingNextCause(3000000, &t, &pauseStat)) == 1);
    ramsyscall_printf("ReadN after Stop: ack stat %02x, first sector in %ius stat %02x, Pause stat %02x\n", readAckStat,
                      readyTime, readyStat, pauseStat);
    cester_assert_uint_eq(3, readCause);
    cester_assert_uint_eq(0x00, readAckStat);
    cester_assert_uint_eq(1, readyCause);
    cester_assert_uint_eq(0x22, readyStat);
    cester_assert_uint_eq(2, cause);
    cester_assert_uint_eq(0x02, pauseStat);
    // The motor spins up first: about 1.6s to 2.9s.
    cester_assert_uint_ge(readyTime, 1200000);
    cester_assert_uint_lt(readyTime, 4500000);
)
