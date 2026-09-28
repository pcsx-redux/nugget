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

// XA-ADPCM real-time playback, against the XA region of the test disc (see
// create-test-iso.lua). Filler sectors carry their LBA in their first 3
// bytes, Form 2 data sectors carry their LBA and 'D'. The tests read sector
// heads at every data ready and check which slots of each 16-sector group
// reached the host, and the ADPBUSY bit of the status register.
//
// The disc is read at double speed and the handler is slower than the drive,
// so some data sectors are missed: the tests check which sectors arrive,
// never how many.

CESTER_BODY(
    struct XAReadStats {
        unsigned events;
        unsigned nonDataCauses;
        unsigned unstamped;
        uint32_t slotMask;
        unsigned form2Seen;
        unsigned busySet;
        unsigned busyClear;
        uint32_t lastLBABusy;
        uint32_t firstLBAClear;
        unsigned busyAfterClear;
        uint32_t minLBA;
        uint32_t maxLBA;
    };

    static int xaCommand(uint8_t command, const uint8_t* params, unsigned count, uint8_t* response,
                         uint8_t* responseSize) {
        CDROM_REG0 = 0;
        for (unsigned i = 0; i < count; i++) CDROM_REG2 = params[i];
        CDROM_REG1 = command;
        uint32_t timeout = 2000000;
        if (!waitCDRomIRQWithTimeout(&timeout)) return -1;
        uint8_t cause = ackCDRomCause();
        uint8_t buffer[16];
        uint8_t size = readResponse(response ? response : buffer);
        if (responseSize) *responseSize = size;
        return cause;
    }

    static int xaWaitCause(uint8_t expected, uint32_t timeoutUs) {
        uint8_t response[16];
        uint32_t timeout = timeoutUs;
        if (!waitCDRomIRQWithTimeout(&timeout)) return 0;
        uint8_t cause = ackCDRomCause();
        readResponse(response);
        return cause == expected;
    }

    static int xaSeekL(uint32_t lba) {
        uint32_t total = lba + 150;
        uint8_t msf[3] = {itob(total / 4500), itob((total / 75) % 60), itob(total % 75)};
        if (xaCommand(CDL_SETLOC, msf, 3, NULL, NULL) != 3) return 0;
        if (xaCommand(CDL_SEEKL, NULL, 0, NULL, NULL) != 3) return 0;
        return xaWaitCause(2, 4000000);
    }

    // Pause, then drain: data ready can still be queued ahead of the
    // acknowledge, so anything but the complete is skipped, bounded.
    static void xaStop() {
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_PAUSE;
        for (unsigned i = 0; i < 64; i++) {
            uint8_t response[16];
            uint32_t timeout = 1000000;
            if (!waitCDRomIRQWithTimeout(&timeout)) return;
            uint8_t cause = ackCDRomCause();
            readResponse(response);
            if (cause == 2) return;
        }
    }

    static void xaReadHead(uint8_t head[4]) {
        CDROM_REG0 = 0;
        CDROM_REG3 = 0x80;
        for (unsigned t = 0; t < 50000; t++) {
            if (CDROM_REG0 & 0x40) break;
        }
        for (int i = 0; i < 4; i++) head[i] = CDROM_REG2;
        CDROM_REG0 = 0;
        CDROM_REG3 = 0;
    }

    // Sets the mode and the filter, ReadS from startLBA for up to durationUs,
    // and records what reached the host. stopAfterClear ends the window once
    // that many events after ADPBUSY cleared have been seen.
    static int xaRead(uint8_t mode, int filter, uint8_t file, uint8_t channel, uint32_t startLBA,
                      uint32_t durationUs, unsigned stopAfterClear, struct XAReadStats* stats) {
        __builtin_memset(stats, 0, sizeof(*stats));
        stats->minLBA = 0xffffffff;
        if (!resetCDRom()) return 0;
        if (xaCommand(CDL_SETMODE, &mode, 1, NULL, NULL) != 3) return 0;
        if (filter) {
            uint8_t params[2] = {file, channel};
            if (xaCommand(CDL_SETFILTER, params, 2, NULL, NULL) != 3) return 0;
        }
        if (!xaSeekL(startLBA)) return 0;
        if (xaCommand(CDL_READS, NULL, 0, NULL, NULL) != 3) return 0;

        initializeTime();
        uint32_t elapsed = 0;
        while ((elapsed < durationUs) && (stats->events < 2000)) {
            uint32_t slice = 30000;
            int got = waitCDRomIRQWithTimeout(&slice);
            elapsed = slice;
            if (!got) continue;
            int busy = (CDROM_REG0 & 0x04) != 0;
            uint8_t cause = ackCDRomCause();
            uint8_t response[16];
            readResponse(response);
            stats->events++;
            if (cause != 1) {
                stats->nonDataCauses++;
                continue;
            }
            uint8_t head[4];
            xaReadHead(head);
            uint32_t lba = head[0] | (head[1] << 8) | (head[2] << 16);
            // Anything without an LBA stamp is a sector that should not have
            // reached the host, most likely an ADPCM one.
            if (lba < startLBA || lba > startLBA + 20000) {
                stats->unstamped++;
                continue;
            }
            if (lba < stats->minLBA) stats->minLBA = lba;
            if (lba > stats->maxLBA) stats->maxLBA = lba;
            stats->slotMask |= 1 << ((lba - startLBA) % 16);
            if (head[3] == 'D') stats->form2Seen++;
            if (busy) {
                stats->busySet++;
                stats->lastLBABusy = lba;
                if (stats->busyClear) stats->busyAfterClear++;
            } else {
                if (!stats->busyClear) stats->firstLBAClear = lba;
                stats->busyClear++;
                if (stopAfterClear && (stats->busyClear >= stopAfterClear)) break;
            }
        }
        xaStop();
        ramsyscall_printf(
            "XA mode %02x filter %i %i/%i from %i: %i events, %i not data ready, %i unstamped, slots %04x, LBA %i-%i, "
            "form 2 %i, ADPBUSY set %i clear %i (last set %i, first clear %i)\n",
            mode, filter, file, channel, startLBA, stats->events, stats->nonDataCauses, stats->unstamped, stats->slotMask,
            stats->minLBA, stats->maxLBA, stats->form2Seen, stats->busySet, stats->busyClear, stats->lastLBABusy,
            stats->firstLBAClear);
        return 1;
    }
)

CESTER_TEST(xaGetParamAfterSetFilter, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }
    uint8_t mode = 0xc8;
    uint8_t filter[2] = {1, 3};
    uint8_t response[16];
    uint8_t size = 0;
    cester_assert_int_eq(3, xaCommand(CDL_SETMODE, &mode, 1, NULL, NULL));
    cester_assert_int_eq(3, xaCommand(CDL_SETFILTER, filter, 2, NULL, NULL));
    cester_assert_int_eq(3, xaCommand(CDL_GETMODE, NULL, 0, response, &size));
    cester_assert_uint_eq(5, size);
    cester_assert_uint_eq(0xc8, response[1]);
    cester_assert_uint_eq(0, response[2]);
    cester_assert_uint_eq(1, response[3]);
    cester_assert_uint_eq(3, response[4]);
    ramsyscall_printf("GetParam: %02x %02x %02x %02x %02x\n", response[0], response[1], response[2], response[3],
                      response[4]);
)

// LBA 135000: 8 channels of file 1 in slots 0-7 of each 16, Form 1 fillers
// in slots 8-15. With RT, SF or both, audio sectors never reach the host,
// whether or not they match the filter. ADPBUSY is set while RT is on.

CESTER_TEST(xaRealTimeFilteredWithholdsAudio, test_instance,
    struct XAReadStats stats;
    int done = xaRead(0xc8, 1, 1, 0, 135000, 1500000, 0, &stats);
    cester_assert_true(done);
    uint32_t fillers = stats.slotMask & 0xff00;
    uint32_t audio = stats.slotMask & 0x00ff;
    cester_assert_uint_ne(0, fillers);
    cester_assert_uint_eq(0, audio);
    cester_assert_uint_eq(0, stats.nonDataCauses);
    cester_assert_uint_eq(0, stats.unstamped);
    cester_assert_uint_eq(0, stats.busyClear);
)

CESTER_TEST(xaRealTimeOtherChannelWithholdsAudio, test_instance,
    struct XAReadStats stats;
    int done = xaRead(0xc8, 1, 1, 3, 135000, 1500000, 0, &stats);
    cester_assert_true(done);
    uint32_t fillers = stats.slotMask & 0xff00;
    uint32_t audio = stats.slotMask & 0x00ff;
    cester_assert_uint_ne(0, fillers);
    cester_assert_uint_eq(0, audio);
    cester_assert_uint_eq(0, stats.nonDataCauses);
    cester_assert_uint_eq(0, stats.unstamped);
    cester_assert_uint_eq(0, stats.busyClear);
)

CESTER_TEST(xaFilterWithoutRealTimeWithholdsAudio, test_instance,
    struct XAReadStats stats;
    int done = xaRead(0x88, 1, 1, 0, 135000, 1500000, 0, &stats);
    cester_assert_true(done);
    uint32_t fillers = stats.slotMask & 0xff00;
    uint32_t audio = stats.slotMask & 0x00ff;
    cester_assert_uint_ne(0, fillers);
    cester_assert_uint_eq(0, audio);
    cester_assert_uint_eq(0, stats.nonDataCauses);
    cester_assert_uint_eq(0, stats.unstamped);
    cester_assert_uint_eq(0, stats.busySet);
)

CESTER_TEST(xaRealTimeUnfilteredWithholdsAudio, test_instance,
    struct XAReadStats stats;
    int done = xaRead(0xc0, 0, 0, 0, 135000, 1500000, 0, &stats);
    cester_assert_true(done);
    uint32_t fillers = stats.slotMask & 0xff00;
    uint32_t audio = stats.slotMask & 0x00ff;
    cester_assert_uint_ne(0, fillers);
    cester_assert_uint_eq(0, audio);
    cester_assert_uint_eq(0, stats.nonDataCauses);
    cester_assert_uint_eq(0, stats.unstamped);
    cester_assert_uint_eq(0, stats.busyClear);
)

// LBA 152744, file 1: slot 0 audio on channel 0, slot 4 Form 2 data,
// slot 8 audio without the RT submode bit, slot 12 audio on channel 0xff.
// Only the Form 2 data sectors and the fillers reach the host.

CESTER_TEST(xaForm2DataReachesHost, test_instance,
    struct XAReadStats stats;
    int done = xaRead(0xc8, 1, 1, 0, 152744, 1500000, 0, &stats);
    cester_assert_true(done);
    cester_assert_uint_ne(0, stats.form2Seen);
    uint32_t form2Slot = stats.slotMask & (1 << 4);
    uint32_t audioSlots = stats.slotMask & ((1 << 0) | (1 << 8) | (1 << 12));
    cester_assert_uint_ne(0, form2Slot);
    cester_assert_uint_eq(0, audioSlots);
    cester_assert_uint_eq(0, stats.nonDataCauses);
    cester_assert_uint_eq(0, stats.unstamped);
)

// LBA 155960, file 1 channel 3 in slot 3 of every 16: EOF alone on its
// sector 24 (LBA 156344), EOR alone on 34 (156504), both on 49 (156744).
// Only the sector with both stops ADPCM: ADPBUSY drops after it and stays
// down, no interrupt other than data ready, and reading carries on.

CESTER_TEST(xaEndOfFileAndRecordStopsADPCM, test_instance,
    struct XAReadStats stats;
    int done = xaRead(0xc8, 1, 1, 3, 155960, 7000000, 20, &stats);
    cester_assert_true(done);
    cester_assert_uint_eq(0, stats.nonDataCauses);
    cester_assert_uint_eq(0, stats.unstamped);
    cester_assert_uint_ne(0, stats.busySet);
    cester_assert_uint_ne(0, stats.busyClear);
    cester_assert_uint_eq(0, stats.busyAfterClear);
    cester_assert_uint_ge(stats.lastLBABusy, 156504);
    cester_assert_uint_gt(stats.firstLBAClear, 156744);
)
