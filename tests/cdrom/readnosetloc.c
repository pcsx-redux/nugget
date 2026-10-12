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

// A read command sent with no new Setloc since the previous read: once while
// the previous read is still delivering sectors, and once after a Pause. Logs
// every interrupt, with the sector header for each data-ready one, and checks
// where the second read starts.

CESTER_BODY(
    typedef struct {
        uint32_t time;
        uint8_t cause;
        uint8_t status;
        uint8_t header[4];
    } NoSetLocEvent;

    // Collects interrupts until `wantData` data-ready ones were seen, the log
    // is full, or 3s pass without an interrupt. Returns the new event count.
    static unsigned noSetLocCollect(NoSetLocEvent* events, unsigned count, unsigned max, unsigned wantData) {
        unsigned dataReady = 0;
        while ((count < max) && (dataReady < wantData)) {
            uint32_t timeout = 3000000;
            if (!waitCDRomIRQWithTimeout(&timeout)) break;
            NoSetLocEvent* e = &events[count++];
            e->time = timeout;
            e->cause = ackCDRomCause();
            uint8_t response[16];
            readResponse(response);
            e->status = response[0];
            e->header[0] = e->header[1] = e->header[2] = e->header[3] = 0;
            if (e->cause == 1) {
                CDROM_REG0 = 0;
                CDROM_REG3 = 0x80;
                while ((CDROM_REG0 & 0x40) == 0);
                for (unsigned i = 0; i < 4; i++) e->header[i] = CDROM_REG2;
                CDROM_REG3 = 0;
                dataReady++;
            }
        }
        return count;
    }

    // Pauses and consumes every interrupt up to the Pause completion, so that
    // nothing is left pending for the next test. Returns 1 once INT2 is seen.
    static int noSetLocPauseAndDrain() {
        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_PAUSE;
        while (1) {
            uint32_t timeout = 3000000;
            if (!waitCDRomIRQWithTimeout(&timeout)) return 0;
            uint8_t cause = ackCDRomCause();
            discardResponse();
            if (cause == 2) return 1;
        }
    }

    static void printNoSetLocEvents(const char* name, const NoSetLocEvent* events, unsigned count, unsigned split) {
        ramsyscall_printf("%s, %u events:", name, count);
        for (unsigned i = 0; i < count; i++) {
            const NoSetLocEvent* e = &events[i];
            if (i == split) ramsyscall_printf(" |");
            ramsyscall_printf(" [%ius int%u stat %02x", e->time, e->cause, e->status);
            if (e->cause == 1) {
                ramsyscall_printf(" hdr %02x:%02x:%02x m%u", e->header[0], e->header[1], e->header[2], e->header[3]);
            }
            ramsyscall_printf("]");
        }
        ramsyscall_printf("\n");
    }

    static uint32_t noSetLocHeaderLBA(const NoSetLocEvent* e) {
        return MSF2LBA(btoi(e->header[0]), btoi(e->header[1]), btoi(e->header[2]));
    }

    // LBA of the last data-ready header in [from, to), or 0 if there is none.
    static uint32_t noSetLocLastLBA(const NoSetLocEvent* events, unsigned from, unsigned to) {
        uint32_t lba = 0;
        for (unsigned i = from; i < to; i++) {
            if (events[i].cause == 1) lba = noSetLocHeaderLBA(&events[i]);
        }
        return lba;
    }

    // LBA of the first data-ready header in [from, to), or 0 if there is none.
    static uint32_t noSetLocFirstLBA(const NoSetLocEvent* events, unsigned from, unsigned to) {
        for (unsigned i = from; i < to; i++) {
            if (events[i].cause == 1) return noSetLocHeaderLBA(&events[i]);
        }
        return 0;
    }
)

CESTER_TEST(readSResentMidStreamNoSetLoc, test_instances,
    NoSetLocEvent events[24];
    int ok = resetCDRom() && setMode(0x20) && setLoc(0, 0x02, 0x16);
    if (!ok) {
        cester_assert_true(ok);
        return;
    }

    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_READS;
    unsigned count = noSetLocCollect(events, 0, 24, 6);
    unsigned split = count;

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_READS;
    count = noSetLocCollect(events, count, 24, 4);
    int drained = noSetLocPauseAndDrain();

    printNoSetLocEvents("ReadS at 00:02:16, six sectors, ReadS again with no Setloc", events, count, split);
    uint32_t before = noSetLocLastLBA(events, 0, split);
    uint32_t after = noSetLocFirstLBA(events, split, count);
    ramsyscall_printf("last LBA before %u, first LBA after %u, Setloc LBA %u\n", before, after, MSF2LBA(0, 2, 16));
    cester_assert_uint_ne(0, before);
    cester_assert_uint_ne(0, after);
    cester_assert_true(drained);
    // No rewind to the Setloc target: the second read carries on with the
    // next sector.
    cester_assert_uint_eq(before + 1, after);
)

CESTER_TEST(readSAfterPauseNoSetLoc, test_instances,
    NoSetLocEvent events[24];
    int ok = resetCDRom() && setMode(0x20) && setLoc(0, 0x02, 0x16);
    if (!ok) {
        cester_assert_true(ok);
        return;
    }

    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_READS;
    unsigned count = noSetLocCollect(events, 0, 24, 6);

    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_PAUSE;
    // Pause ack, then its completion; a sector or two can still land between.
    unsigned paused = 0;
    while ((count < 24) && !paused) {
        unsigned prev = count;
        count = noSetLocCollect(events, count, prev + 1, 1);
        if (count == prev) break;
        if (events[count - 1].cause == 2) paused = 1;
    }
    unsigned split = count;

    initializeTime();
    while (updateTime() < 100000);

    initializeTime();
    CDROM_REG0 = 0;
    CDROM_REG1 = CDL_READS;
    count = noSetLocCollect(events, count, 24, 4);
    int drained = noSetLocPauseAndDrain();

    printNoSetLocEvents("ReadS at 00:02:16, six sectors, Pause, ReadS again with no Setloc", events, count, split);
    uint32_t before = noSetLocLastLBA(events, 0, split);
    uint32_t after = noSetLocFirstLBA(events, split, count);
    ramsyscall_printf("last LBA before %u, first LBA after %u, Setloc LBA %u\n", before, after, MSF2LBA(0, 2, 16));
    cester_assert_true(paused);
    cester_assert_true(drained);
    cester_assert_uint_ne(0, before);
    cester_assert_uint_ne(0, after);
    // Resumes one sector before the last one delivered, not at the Setloc
    // target (00:02:20 after 00:02:21 on a SCPH-9002, three runs out of three).
    cester_assert_uint_eq(before - 1, after);
)
