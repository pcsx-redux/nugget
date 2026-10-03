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

// CD-DA level through the ATV matrix, read back from the SPU CD capture
// buffer. Track 2 of the test disc is a 450 Hz sine, identical on both
// channels, with a peak of 24987.

// Pre-fill for the capture region, small enough that an untouched buffer fails every level check.
#define CDDA_MARKER 0x0101
#define CDDA_SPU_DELAY (*(volatile uint32_t *)0xbf801014)

CESTER_BODY(
    static uint16_t s_cddaCapture[0x400];

    static int cddaSpuDma(uint32_t spuAddr, void *buf, uint32_t bytes, int isRead) {
        const uint16_t tsa = spuAddr >> 3;
        const uint16_t mode = isRead ? 0x30 : 0x20;
        SPU_RAM_DTA = tsa;
        for (int i = 0; i < 0xf01 && SPU_RAM_DTA != tsa; i++);
        SPU_CTRL = (SPU_CTRL & ~0x30) | mode;
        for (volatile int i = 0; i < 60; i++);
        for (int i = 0; i < 0xf01 && (SPU_STATUS & 0x30) != mode; i++);
        CDDA_SPU_DELAY = (CDDA_SPU_DELAY & 0xf0ffffff) | (isRead ? 0x22000000 : 0x20000000);
        DMA_CTRL[DMA_SPU].MADR = (uint32_t)buf & 0x1fffffff;
        DMA_CTRL[DMA_SPU].BCR = ((bytes >> 6) << 16) | 0x10;
        DMA_CTRL[DMA_SPU].CHCR = isRead ? 0x01000200 : 0x01000201;
        int ok = 0;
        for (unsigned t = 0; t < 4000000u; t++) {
            if ((DMA_CTRL[DMA_SPU].CHCR & 0x01000000) == 0) {
                ok = 1;
                break;
            }
        }
        SPU_CTRL = SPU_CTRL & ~0x30;
        for (volatile int i = 0; i < 60; i++);
        return ok;
    }

    static int cddaSimpleCmd(uint8_t cmd) {
        uint8_t response[16];
        CDROM_REG0 = 0;
        CDROM_REG1 = cmd;
        uint32_t timeout = 2000000;
        int ok = waitCDRomIRQWithTimeout(&timeout);
        ackCDRomCause();
        readResponse(response);
        return ok;
    }

    static void cddaWaitPauseComplete() {
        initializeTime();
        uint32_t elapsed = 0;
        while (elapsed < 5000000u) {
            uint32_t budget = 200000u;
            int ok = waitCDRomIRQWithTimeout(&budget);
            elapsed = budget;
            if (!ok) continue;
            uint8_t cause = ackCDRomCause();
            uint8_t response[16];
            readResponse(response);
            if (cause == 2) return;
        }
    }

    // Resets the drive, pre-fills the SPU capture region, sets mute or demute and the ATV
    // matrix, and turns on CD audio in the SPU. 0 on a failed step.
    static int cddaAudioSetup(const uint8_t atv[4], int demute) {
        if (!resetCDRom()) return 0;

        DPCR |= 0x000b0000;
        SPU_CTRL = 0;
        for (volatile int i = 0; i < 20000; i++);
        SPU_VOL_MAIN_LEFT = 0;
        SPU_VOL_MAIN_RIGHT = 0;
        SPU_KEY_OFF_LOW = 0xffff;
        SPU_KEY_OFF_HIGH = 0xffff;
        SPU_RAM_DTC = 4;
        SPU_CTRL = 0x8001;
        for (volatile int i = 0; i < 20000; i++);
        for (int i = 0; i < 0x400; i++) s_cddaCapture[i] = CDDA_MARKER;
        if (!cddaSpuDma(0x000, s_cddaCapture, 0x800, 0)) return 0;

        if (!cddaSimpleCmd(demute ? CDL_DEMUTE : CDL_MUTE)) return 0;

        CDROM_REG0 = 2;
        CDROM_REG2 = atv[0];
        CDROM_REG3 = atv[1];
        CDROM_REG0 = 3;
        CDROM_REG1 = atv[2];
        CDROM_REG2 = atv[3];
        CDROM_REG0 = 3;
        CDROM_REG3 = 0x20;
        CDROM_REG0 = 0;

        SPU_VOL_CD_LEFT = 0x7fff;
        SPU_VOL_CD_RIGHT = 0x7fff;
        SPU_CTRL = SPU_CTRL | 0x8001;
        return 1;
    }

    // Peak and number of sign changes of the left side of the capture buffer.
    static int cddaBufferPeak(unsigned *signChanges) {
        int peak = 0;
        unsigned changes = 0;
        int prev = 0;
        for (int i = 0; i < 0x200; i++) {
            int v = (int16_t)s_cddaCapture[i];
            int a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            int sign = v >= 0 ? 1 : -1;
            if (i && sign != prev) changes++;
            prev = sign;
        }
        *signChanges = changes;
        return peak;
    }

    // Reads the capture buffer back while audio plays. On a SCPH-7502 the capture sometimes
    // reads all zeros for a while after the audio should have started, so an empty capture is
    // retried a few times, 250 ms apart. -1 on a failed DMA.
    static int cddaCapturePeak(unsigned *signChanges) {
        int peak = 0;
        for (unsigned attempt = 0; attempt < 8; attempt++) {
            if (attempt) {
                initializeTime();
                while (updateTime() < 250000u);
            }
            if (!cddaSpuDma(0x000, s_cddaCapture, 0x800, 1)) return -1;
            peak = cddaBufferPeak(signChanges);
            if (peak) {
                if (attempt) ramsyscall_printf("capture was empty %u time(s) before audio\n", attempt);
                break;
            }
        }
        return peak;
    }

    // Plays track 2 with the given ATV, captures 512 samples per side while playing,
    // and returns the left side's peak and number of sign changes. -1 on a failed step.
    static int cddaPeak(const uint8_t atv[4], int demute, unsigned *signChanges) {
        if (!cddaAudioSetup(atv, demute)) return -1;
        if (!setMode(0)) return -1;

        CDROM_REG0 = 0;
        CDROM_REG2 = 0x02;
        if (!cddaSimpleCmd(CDL_PLAY)) return -1;

        initializeTime();
        while (updateTime() < 1500000u);

        int peak = cddaCapturePeak(signChanges);

        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_PAUSE;
        cddaWaitPauseComplete();

        // Leave the head on the data track: a GetLocL with an audio sector as the last one
        // read answers with an error, and the next test reads the location after a reset.
        CDROM_REG0 = 0;
        CDROM_REG2 = 0x00;
        CDROM_REG2 = 0x02;
        CDROM_REG2 = 0x16;
        if (cddaSimpleCmd(CDL_SETLOC) && cddaSimpleCmd(CDL_SEEKL)) cddaWaitPauseComplete();

        return peak;
    }
)

CESTER_TEST(cddaLevelUnity, test_instances,
    static const uint8_t atv[4] = {0x80, 0x00, 0x80, 0x00};
    unsigned changes = 0;
    int peak = cddaPeak(atv, 1, &changes);
    ramsyscall_printf("CD-DA level at unity ATV: peak %i, %u sign changes\n", peak, changes);
    cester_assert_int_ge(peak, 22800);
    cester_assert_int_le(peak, 23400);
    cester_assert_uint_ge(changes, 4);
)

CESTER_TEST(cddaLevelHalf, test_instances,
    static const uint8_t atv[4] = {0x40, 0x00, 0x40, 0x00};
    unsigned changes = 0;
    int peak = cddaPeak(atv, 1, &changes);
    ramsyscall_printf("CD-DA level at half ATV: peak %i, %u sign changes\n", peak, changes);
    cester_assert_int_ge(peak, 11300);
    cester_assert_int_le(peak, 11800);
    cester_assert_uint_ge(changes, 4);
)

CESTER_TEST(cddaLevelClipped, test_instances,
    static const uint8_t atv[4] = {0x80, 0x80, 0x80, 0x80};
    unsigned changes = 0;
    int peak = cddaPeak(atv, 1, &changes);
    ramsyscall_printf("CD-DA level at double ATV: peak %i, %u sign changes\n", peak, changes);
    cester_assert_int_ge(peak, 31700);
    cester_assert_int_le(peak, 32050);
    cester_assert_uint_ge(changes, 4);
)

CESTER_TEST(cddaMuted, test_instances,
    static const uint8_t atv[4] = {0x80, 0x00, 0x80, 0x00};
    unsigned changes = 0;
    int peak = cddaPeak(atv, 0, &changes);
    ramsyscall_printf("CD-DA level when muted: peak %i\n", peak);
    cester_assert_int_eq(0, peak);
)
