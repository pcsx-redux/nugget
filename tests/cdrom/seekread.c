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

// A read command sent while a SeekP is still running. Logs every interrupt
// that follows, with the sector header for each data-ready one.

CESTER_BODY(
    typedef struct {
        uint32_t time;
        uint8_t cause;
        uint8_t status;
        uint8_t header[4];
    } SeekReadEvent;

    static unsigned seekReadDuringSeekP(uint8_t readCommand, uint8_t fromS, uint8_t fromF, uint8_t toS,
                                        uint8_t toF, SeekReadEvent events[8], uint32_t* ackTime) {
        if (!resetCDRom()) return 0;
        if (!setMode(0x20)) return 0;
        if (!seekPTo(0, fromS, fromF)) return 0;
        if (!setLoc(0, toS, toF)) return 0;

        initializeTime();
        while (updateTime() < 50000);
        initializeTime();

        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_SEEKP;
        *ackTime = waitCDRomIRQ();
        ackCDRomCause();
        discardResponse();

        initializeTime();
        CDROM_REG0 = 0;
        CDROM_REG1 = readCommand;

        unsigned count = 0;
        unsigned dataReady = 0;
        while (count < 8) {
            uint32_t timeout = 3000000;
            if (!waitCDRomIRQWithTimeout(&timeout)) break;
            SeekReadEvent* e = &events[count++];
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
                if (++dataReady == 2) {
                    CDROM_REG0 = 0;
                    CDROM_REG1 = CDL_PAUSE;
                }
            }
        }
        return count;
    }

    static void printSeekReadEvents(const char* name, uint32_t ackTime, const SeekReadEvent* events,
                                    unsigned count) {
        ramsyscall_printf("%s, seekP ack in %ius, %u events:", name, ackTime, count);
        for (unsigned i = 0; i < count; i++) {
            const SeekReadEvent* e = &events[i];
            ramsyscall_printf(" [%ius int%u stat %02x", e->time, e->cause, e->status);
            if (e->cause == 1) {
                ramsyscall_printf(" hdr %02x:%02x:%02x m%u", e->header[0], e->header[1], e->header[2], e->header[3]);
            }
            ramsyscall_printf("]");
        }
        ramsyscall_printf("\n");
    }

    static int firstDataHeaderIs(const SeekReadEvent* events, unsigned count, uint8_t s, uint8_t f) {
        for (unsigned i = 0; i < count; i++) {
            if (events[i].cause != 1) continue;
            return (events[i].header[0] == 0) && (events[i].header[1] == s) && (events[i].header[2] == f);
        }
        return 0;
    }
)

CESTER_TEST(readNDuringSeekPFar, test_instances,
    SeekReadEvent events[8];
    uint32_t ackTime = 0;
    unsigned count = seekReadDuringSeekP(CDL_READN, 0x02, 0x00, 0x04, 0x00, events, &ackTime);
    printSeekReadEvents("ReadN during seekP 00:02:00 to 00:04:00", ackTime, events, count);
    cester_assert_true(firstDataHeaderIs(events, count, 0x04, 0x00));
)

CESTER_TEST(readSDuringSeekPSamePlace, test_instances,
    SeekReadEvent events[8];
    uint32_t ackTime = 0;
    unsigned count = seekReadDuringSeekP(CDL_READS, 0x02, 0x16, 0x02, 0x16, events, &ackTime);
    printSeekReadEvents("ReadS during seekP 00:02:16 to 00:02:16", ackTime, events, count);
    cester_assert_true(firstDataHeaderIs(events, count, 0x02, 0x16));
)
