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

// clang-format off

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

    // Reads the capture buffer back while audio plays. On a SCPH-9002 the capture sometimes
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

// CD audio into the reverb unit. SPUCNT bit 2 sends CD audio to the reverb input, and the
// reverb unit writes its input into the work area, so the work area shows whether CD audio
// reached it without any capture of the analog output. Bit 0 gates the send as well.
#define CDDA_REVERB_BASE 0xcff8
#define CDDA_REVERB_SIZE 0x18040u
#define CDDA_REVERB_CHUNK 0x2000u
#define CDDA_REVERB_SLACK 0x100u

CESTER_BODY(
    static uint8_t s_cddaReverbBuf[CDDA_REVERB_CHUNK + CDDA_REVERB_SLACK] __attribute__((aligned(4)));

    // Delay from psx-spx's reverb presets: the input is written to mLSAME/mRSAME every tick.
    static const uint16_t c_cddaDelay[32] = {
        0x0001, 0x0001, 0x7fff, 0x7fff, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1fff, 0x0fff, 0x1005, 0x0005, 0x0000, 0x0000,
        0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000,
    };

    // Zeroes the work area, or counts the halfwords of it that are not zero. Reads ask for
    // CDDA_REVERB_SLACK more bytes than they keep: the end of a DMA read can come back wrong.
    static int cddaReverbArea(int count) {
        int nonZero = 0;
        uint32_t addr = CDDA_REVERB_BASE * 8;
        uint32_t left = CDDA_REVERB_SIZE;
        if (!count) __builtin_memset(s_cddaReverbBuf, 0, sizeof(s_cddaReverbBuf));
        while (left) {
            uint32_t n = left > CDDA_REVERB_CHUNK ? CDDA_REVERB_CHUNK : left;
            if (count) {
                if (!cddaSpuDma(addr, s_cddaReverbBuf, CDDA_REVERB_CHUNK + CDDA_REVERB_SLACK, 1)) return -1;
                const uint16_t *h = (const uint16_t *)s_cddaReverbBuf;
                for (uint32_t i = 0; i < n / 2; i++) nonZero += h[i] != 0;
            } else {
                if (!cddaSpuDma(addr, s_cddaReverbBuf, n, 0)) return -1;
            }
            addr += n;
            left -= n;
        }
        return nonZero;
    }

    // Plays track 2 for a second with reverb on and the given SPUCNT CD bits, and returns how
    // many halfwords of the work area the reverb unit wrote. -1 on a failed step.
    static int cddaReverbWrites(uint16_t cdBits) {
        static const uint8_t atv[4] = {0x80, 0x00, 0x80, 0x00};
        if (!cddaAudioSetup(atv, 1)) return -1;
        if (cddaReverbArea(0) < 0) return -1;
        volatile uint16_t *regs = (volatile uint16_t *)SPU_REVERB;
        for (int i = 0; i < 32; i++) regs[i] = c_cddaDelay[i];
        SPU_REVERB_ADDR = CDDA_REVERB_BASE;
        if (!setMode(0)) return -1;

        CDROM_REG0 = 0;
        CDROM_REG2 = 0x02;
        if (!cddaSimpleCmd(CDL_PLAY)) return -1;

        initializeTime();
        while (updateTime() < 1500000u);
        SPU_CTRL = 0x8000 | 0x0080 | cdBits;
        initializeTime();
        while (updateTime() < 1000000u);
        SPU_CTRL = 0x8001;
        for (volatile int i = 0; i < 20000; i++);

        int written = cddaReverbArea(1);
        SPU_REVERB_ADDR = 0xffff;

        CDROM_REG0 = 0;
        CDROM_REG1 = CDL_PAUSE;
        cddaWaitPauseComplete();

        // Same head parking as cddaPeak.
        CDROM_REG0 = 0;
        CDROM_REG2 = 0x00;
        CDROM_REG2 = 0x02;
        CDROM_REG2 = 0x16;
        if (cddaSimpleCmd(CDL_SETLOC) && cddaSimpleCmd(CDL_SEEKL)) cddaWaitPauseComplete();

        return written;
    }
)

CESTER_TEST(cddaReverbOff, test_instances,
    int written = cddaReverbWrites(0x0001);
    ramsyscall_printf("CD-DA into reverb, SPUCNT bit 2 clear: %i halfwords written\n", written);
    cester_assert_int_eq(0, written);
)

CESTER_TEST(cddaReverbOn, test_instances,
    int written = cddaReverbWrites(0x0005);
    ramsyscall_printf("CD-DA into reverb, SPUCNT bits 0 and 2 set: %i halfwords written\n", written);
    // Both sides fill the whole area (49184 halfwords on a SCPH-9002); one side alone writes
    // about a third of it, so a lost channel fails here.
    cester_assert_int_ge(written, 40000);
)

CESTER_TEST(cddaReverbNeedsCdEnable, test_instances,
    int written = cddaReverbWrites(0x0004);
    ramsyscall_printf("CD-DA into reverb, SPUCNT bit 2 set, bit 0 clear: %i halfwords written\n", written);
    cester_assert_int_eq(0, written);
)
