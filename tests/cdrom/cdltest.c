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

CESTER_TEST(cdlTestNoArg, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    initializeTime();

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_TEST;

    uint32_t errorTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t ctrl1 = CDROM_REG0 & ~3;
    uint8_t response1[16];
    uint8_t responseSize1 = readResponse(response1);
    uint8_t ctrl2 = CDROM_REG0 & ~3;
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;

    cester_assert_uint_eq(5, cause1);
    cester_assert_uint_eq(0xe0, cause1b);
    cester_assert_uint_eq(3, response1[0]);
    cester_assert_uint_eq(0x20, response1[1]);
    cester_assert_uint_eq(2, responseSize1);
    cester_assert_uint_eq(0x38, ctrl1);
    cester_assert_uint_eq(0x18, ctrl2);
    cester_assert_uint_ge(errorTime, 500);
    cester_assert_uint_lt(errorTime, 7000);
    ramsyscall_printf("Basic cdlTest with no args, errored in %ius\n", errorTime);
)

CESTER_TEST(cdlTest20, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    initializeTime();

    CDROM_REG0 = 0;
    CDROM_REG2 = 0x20;
    CDROM_REG1 = CDL_TEST;

    uint32_t ackTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t ctrl1 = CDROM_REG0 & ~3;
    uint8_t response1[16];
    uint8_t responseSize1 = readResponse(response1);
    uint8_t ctrl2 = CDROM_REG0 & ~3;
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;

    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(0xe0, cause1b);
    cester_assert_uint_eq(4, responseSize1);
    cester_assert_uint_eq(0x38, ctrl1);
    cester_assert_uint_eq(0x18, ctrl2);
    cester_assert_uint_ge(ackTime, 500);
    cester_assert_uint_lt(ackTime, 7000);
    ramsyscall_printf("Basic cdlTest with arg = 0x20, ack in %ius, response = %02x %02x %02x %02x\n", ackTime, response1[0], response1[1], response1[2], response1[3]);
)

CESTER_TEST(cdlTest20ExtraArgs, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    initializeTime();

    CDROM_REG0 = 0;
    CDROM_REG2 = 0x20;
    CDROM_REG2 = 0x00;
    CDROM_REG1 = CDL_TEST;

    uint32_t errorTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t ctrl1 = CDROM_REG0 & ~3;
    uint8_t response1[16];
    uint8_t responseSize1 = readResponse(response1);
    uint8_t ctrl2 = CDROM_REG0 & ~3;
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;

    cester_assert_uint_eq(5, cause1);
    cester_assert_uint_eq(0xe0, cause1b);
    cester_assert_uint_eq(3, response1[0]);
    cester_assert_uint_eq(0x20, response1[1]);
    cester_assert_uint_eq(2, responseSize1);
    cester_assert_uint_eq(0x38, ctrl1);
    cester_assert_uint_eq(0x18, ctrl2);
    cester_assert_uint_ge(errorTime, 500);
    cester_assert_uint_lt(errorTime, 7000);
    ramsyscall_printf("Basic cdlTest with arg = 0x20, 0x00, errored in %ius\n", errorTime);
)

CESTER_TEST(cdlTestff, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    initializeTime();

    CDROM_REG0 = 0;
    CDROM_REG2 = 0xff;
    CDROM_REG1 = CDL_TEST;

    uint32_t ackTime = waitCDRomIRQ();
    uint8_t cause1 = ackCDRomCause();
    uint8_t ctrl1 = CDROM_REG0 & ~3;
    uint8_t response1[16];
    uint8_t responseSize1 = readResponse(response1);
    uint8_t ctrl2 = CDROM_REG0 & ~3;
    CDROM_REG0 = 1;
    uint8_t cause1b = CDROM_REG3_UC;

    cester_assert_uint_eq(5, cause1);
    cester_assert_uint_eq(0xe0, cause1b);
    cester_assert_uint_eq(3, response1[0]);
    cester_assert_uint_eq(0x10, response1[1]);
    cester_assert_uint_eq(2, responseSize1);
    cester_assert_uint_eq(0x38, ctrl1);
    cester_assert_uint_eq(0x18, ctrl2);
    cester_assert_uint_ge(ackTime, 500);
    cester_assert_uint_lt(ackTime, 7000);
    ramsyscall_printf("Basic cdlTest with arg = 0xff, errored in %ius\n", ackTime);
)

CESTER_BODY(
    // Sends Test with one subfunction byte and records the response.
    // Whether the response is exactly one of the strings in the list.
    static int cdlTestKnownString(const char* const* list, unsigned count, const uint8_t* response, uint8_t size) {
        for (unsigned i = 0; i < count; i++) {
            unsigned len = 0;
            while (list[i][len]) len++;
            if (len == size && __builtin_memcmp(list[i], response, size) == 0) return 1;
        }
        return 0;
    }

    static int cdlTestSub(uint8_t sub, uint8_t* cause, uint8_t response[16], uint8_t* size, uint32_t* time) {
        __builtin_memset(response, 0xff, 16);
        initializeTime();
        CDROM_REG0 = 0;
        CDROM_REG2 = sub;
        CDROM_REG1 = CDL_TEST;
        uint32_t timeout = 1000000;
        int ok = waitCDRomIRQWithTimeout(&timeout);
        *time = timeout;
        *cause = ok ? ackCDRomCause() : 0;
        *size = ok ? readResponse(response) : 0;
        ramsyscall_printf("cdlTest %02x: ok %i cause %i size %i in %ius:", sub, ok, *cause, *size, *time);
        for (unsigned i = 0; i < *size; i++) ramsyscall_printf(" %02x", response[i]);
        ramsyscall_printf("\n");
        return ok;
    }
)

CESTER_TEST(cdlTestRegionAndChip, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    // 22h answers the region string and 23h/24h the servo and signal processor chips, as
    // plain text with no status byte. A SCPH-9002 (vC3) answers "for Europe" and "CXD2940Q".
    // psx-spx: vC0 controllers do not have these subfunctions and answer error 11h 10h; only
    // the 10h is checked, as no vC0 console has been measured.
    static const char* regions[] = {"for Europe", "for U/C", "for Japan", "for NETNA", "for US/AEP"};
    static const char* servoChips[] = {"CXD2940Q", "CXD1817Q", "CXD2545Q", "CXD1782BR"};
    static const char* signalChips[] = {"CXD2940Q", "CXD1817Q", "CXD2545Q", "CXD2510Q"};
    uint8_t cause, size, response[16];
    uint32_t time;

    // The version is the last byte of the Test 20h answer.
    cester_assert_true(cdlTestSub(0x20, &cause, response, &size, &time));
    cester_assert_uint_eq(4, size);
    if (size != 4) return;
    uint8_t version = response[3];

    for (uint8_t sub = 0x22; sub <= 0x24; sub++) {
        cester_assert_true(cdlTestSub(sub, &cause, response, &size, &time));
        if (version == 0xc0) {
            cester_assert_uint_eq(5, cause);
            cester_assert_uint_eq(2, size);
            cester_assert_uint_eq(0x10, response[1]);
            continue;
        }
        cester_assert_uint_eq(3, cause);
        if (sub == 0x22) {
            cester_assert_true(cdlTestKnownString(regions, sizeof(regions) / sizeof(regions[0]), response, size));
        } else if (sub == 0x23) {
            cester_assert_true(cdlTestKnownString(servoChips, sizeof(servoChips) / sizeof(servoChips[0]), response, size));
        } else {
            cester_assert_true(cdlTestKnownString(signalChips, sizeof(signalChips) / sizeof(signalChips[0]), response, size));
        }
    }
)

CESTER_MAYBE_TEST(cdlTestCountersSwitchesDecoder, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    // 05h answers the SCEx counters (total, success), 21h the switches (bit 0 head at
    // POS0, bit 1 door open), 25h the decoder chip string. psx-spx: 25h is vC1 and up,
    // vC0 errors with 10h.
    // Seen on silicon: 05h 01 01 on a SCPH-7001 and 00 00 on a SCPH-5501 and a SCPH-1001
    // with no readable disc; a modchipped SCPH-9002 counts every looped string, 09 01.
    // 21h 00 on a SCPH-9002 spinning a disc, 01 on idle drives, 02 with the lid open.
    // 25h CXD2940Q (SCPH-9002), CXD1817Q (SCPH-7001), CXD1815Q (SCPH-5501), CXD1199BQ
    // (SCPH-1001).
    static const char* decoderChips[] = {"CXD2940Q", "CXD1817Q", "CXD1815Q", "CXD1199BQ", "CXD1815BQ"};
    uint8_t cause, size, response[16];
    uint32_t time;

    cester_assert_true(cdlTestSub(0x20, &cause, response, &size, &time));
    cester_assert_uint_eq(4, size);
    if (size != 4) return;
    uint8_t version = response[3];

    cester_assert_true(cdlTestSub(0x05, &cause, response, &size, &time));
    cester_assert_uint_eq(3, cause);
    cester_assert_uint_eq(2, size);
    uint8_t total = response[0];
    uint8_t success = response[1];
    cester_assert_uint_le(success, total);

    // Bits 0 and 1 follow the head and the lid, so only the unused bits are checked.
    cester_assert_true(cdlTestSub(0x21, &cause, response, &size, &time));
    cester_assert_uint_eq(3, cause);
    cester_assert_uint_eq(1, size);
    uint8_t switches = response[0] & 0xfc;
    cester_assert_uint_eq(0, switches);

    cester_assert_true(cdlTestSub(0x25, &cause, response, &size, &time));
    if (version == 0xc0) {
        cester_assert_uint_eq(5, cause);
        cester_assert_uint_eq(2, size);
        cester_assert_uint_eq(0x10, response[1]);
        return;
    }
    cester_assert_uint_eq(3, cause);
    cester_assert_true(cdlTestKnownString(decoderChips, sizeof(decoderChips) / sizeof(decoderChips[0]), response, size));
)

CESTER_BODY(
    // Sends Test 21h on the running clock (no initializeTime, so a phase keeps one time base),
    // then Nop for the status byte, and prints one tagged line.
    // Returns the 21h byte, or 0xff if the reply was not one byte with cause 3.
    static uint8_t cdlT21Sample(const char* phase, int* ok) {
        uint8_t response[16];
        __builtin_memset(response, 0xff, 16);
        uint32_t t = updateTime();
        CDROM_REG0 = 0;
        CDROM_REG2 = 0x21;
        CDROM_REG1 = CDL_TEST;
        uint32_t timeout = 1000000;
        int irq = waitCDRomIRQWithTimeout(&timeout);
        uint8_t cause = irq ? ackCDRomCause() : 0;
        uint8_t size = irq ? readResponse(response) : 0;
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_NOP;
        timeout = 1000000;
        irq = waitCDRomIRQWithTimeout(&timeout);
        uint8_t nopCause = irq ? ackCDRomCause() : 0;
        uint8_t stat[16];
        uint8_t nopSize = irq ? readResponse(stat) : 0;
        ramsyscall_printf("T21 phase=%s t=%u v=%02x cause=%u size=%u stat=%02x\n", phase, t / 1000, response[0], cause,
                          size, (nopCause == 3 && nopSize >= 1) ? stat[0] : 0xff);
        if ((cause != 3) || (size != 1) || (response[0] & 0xfc)) {
            *ok = 0;
            return 0xff;
        }
        return response[0];
    }

    // Samples 21h every 200ms from now (time 0) to `durationMs` inclusive.
    static void cdlT21Poll(const char* phase, uint32_t durationMs, int* ok) {
        initializeTime();
        for (uint32_t ms = 0; ms <= durationMs; ms += 200) {
            while (updateTime() < ms * 1000);
            cdlT21Sample(phase, ok);
        }
    }

    // Sends a parameterless command and waits for its completion (INT2), acking and
    // discarding anything else on the way (stray INT1 sectors, the INT3 ack).
    // Returns the completion's status byte, or 0xff on an error or a timeout.
    static uint8_t cdlT21Complete(uint8_t command, uint32_t timeoutUs, uint32_t* completeTime) {
        initializeTime();
        CDROM_REG0 = 0;
        CDROM_REG1 = command;
        while (1) {
            uint32_t timeout = timeoutUs;
            if (!waitCDRomIRQWithTimeout(&timeout)) return 0xff;
            uint8_t cause = ackCDRomCause();
            uint8_t response[16];
            response[0] = 0xff;
            readResponse(response);
            if (cause == 2) {
                *completeTime = timeout;
                return response[0];
            }
            if (cause == 5) return 0xff;
        }
    }
)

CESTER_MAYBE_TEST(cdlTestSwitchesAfterStop, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }

    // Records 21h (bit 0 head at POS0, bit 1 lid open) across Stop, Init and Pause. Silicon
    // so far: 00 on a SCPH-9002 spinning a disc, 01 on idle drives. Whether bit 0 comes back
    // once the drive stops, and how soon, is what this prints; only the reply shape and the
    // unused bits are checked. Lines are tagged "T21 phase=<p> t=<ms since phase start>".
    int ok = 1;
    uint32_t completeTime = 0;

    cdlT21Poll("spin", 0, &ok);

    uint8_t stopStat = cdlT21Complete(CDL_STOP, 5000000, &completeTime);
    ramsyscall_printf("T21 Stop complete stat %02x in %ius\n", stopStat, completeTime);
    cester_assert_uint_ne(0xff, stopStat);
    cdlT21Poll("stop", 2000, &ok);

    uint8_t initStat = cdlT21Complete(CDL_INIT, 6000000, &completeTime);
    ramsyscall_printf("T21 Init complete stat %02x in %ius\n", initStat, completeTime);
    cester_assert_uint_ne(0xff, initStat);
    cester_assert_true(setMode(0));
    cdlT21Poll("init", 2000, &ok);

    // A short ReadN so the head has moved off wherever Init left it, then Pause with the
    // motor still on.
    cester_assert_true(setLoc(0x00, 0x02, 0x16));
    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_READN;
    unsigned sectors = 0;
    while (sectors < 4) {
        uint32_t timeout = 5000000;
        if (!waitCDRomIRQWithTimeout(&timeout)) break;
        uint8_t cause = ackCDRomCause();
        discardResponse();
        if (cause == 1) sectors++;
        if (cause == 5) break;
    }
    ramsyscall_printf("T21 ReadN sectors %u\n", sectors);
    cester_assert_uint_eq(4, sectors);
    uint8_t pauseStat = cdlT21Complete(CDL_PAUSE, 3000000, &completeTime);
    ramsyscall_printf("T21 Pause complete stat %02x in %ius\n", pauseStat, completeTime);
    cester_assert_uint_ne(0xff, pauseStat);
    cdlT21Poll("pause", 1000, &ok);

    cester_assert_true(ok);

    // Leave the drive spinning in mode 0 for whatever runs next, as resetCDRom does.
    cester_assert_true(resetCDRom());
)
