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

// The real player, built into the test so its state can be read back. cester
// includes this file again to register the tests, hence the guard.
#ifndef XMPLAYER_STATE_PLAYER
#define XMPLAYER_STATE_PLAYER
#include "common/syscalls/syscalls.h"
#include "xmplayer/xmplayer.c"
#endif

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
    // A 2 channels song with one pattern of 8 rows at speed 1, so each poll
    // plays one row, built in memory. One instrument maps five notes to five
    // samples, one per version 2 sample flag. sampleBytes is 0 and XM_Load
    // gets no samples, so nothing is uploaded: the voices play whatever SPU
    // RAM holds, and the tests only read back the player state and registers.
    enum {
        ORDERS = 72, PATTERNS = 76, INSTRUMENTS = 84, SAMPLES = 292, REVERB = 452, DATA = 524,
        ROWS = 8, CHANNELS = 2,
        PLAIN = 49, HWADSR = 50, VERB = 51, NOISE = 52, PMOD = 53, NOTE_OFF = 97,
        ADSR = 0x9fc080ff,
    };
    static uint8_t s_file[768] __attribute__((aligned(4)));
    struct Cell { uint8_t note, ext, fx, param; uint16_t extParam; };
    static struct Cell s_cells[ROWS][CHANNELS];

    // version 1 or 2; for version 1, tickRate and reverbOffset are written
    // too, and the player has to ignore them
    static void buildSong(unsigned version, uint32_t tickRate, int reverb) {
        __builtin_memset(s_file, 0, sizeof(s_file));
        __builtin_memset(s_cells, 0, sizeof(s_cells));
        struct XMSongHeader *h = (struct XMSongHeader *)s_file;
        h->magic[0] = 'P'; h->magic[1] = 'S'; h->magic[2] = 'X'; h->magic[3] = 'M';
        h->version = version;
        h->headerSize = version >= 2 ? 72 : 64;
        h->channels = CHANNELS;
        h->flags = 1;
        h->speed = 1;
        h->bpm = 125;
        h->songLength = 1;
        h->numPatterns = 1;
        h->numInstruments = 1;
        h->numSamples = 5;
        h->ordersOffset = ORDERS;
        h->patternsOffset = PATTERNS;
        h->instrumentsOffset = INSTRUMENTS;
        h->samplesOffset = SAMPLES;
        h->tickRate = tickRate;
        h->reverbOffset = reverb ? REVERB : 0;
        struct XMInstrument *ins = (struct XMInstrument *)(s_file + INSTRUMENTS);
        __builtin_memset(ins->keymap, 0xff, sizeof(ins->keymap));
        static const uint8_t flags[5] = {0, 4, 8, 16, 32};
        struct XMSample *s = (struct XMSample *)(s_file + SAMPLES);
        for (unsigned i = 0; i < 5; i++) {
            ins->keymap[PLAIN - 1 + i] = i;
            s[i].spuLength = 32;
            s[i].length = 28;
            s[i].loopLength = 28;
            s[i].flags = 1 | flags[i];
            s[i].volume = 64;
            s[i].pan = 128;
            s[i].adsr = flags[i] == 4 ? ADSR : 0;
        }
        struct XMReverb *r = (struct XMReverb *)(s_file + REVERB);
        r->regs[30] = r->regs[31] = 0x2000;
        r->base = 0xfe00;
        r->volLeft = 0x1000;
        r->volRight = 0x0800;
    }

    static void setCell(unsigned row, unsigned channel, unsigned note, unsigned fx, unsigned param, unsigned ext, unsigned extParam) {
        struct Cell *c = &s_cells[row][channel];
        c->note = note; c->fx = fx; c->param = param; c->ext = ext; c->extParam = extParam;
    }

    // Packs the cells, every one with a mask byte, and loads the song.
    static unsigned load() {
        s_file[ORDERS] = 0;
        uint8_t *p = s_file + DATA;
        for (unsigned r = 0; r < ROWS; r++) {
            for (unsigned c = 0; c < CHANNELS; c++) {
                const struct Cell *cell = &s_cells[r][c];
                uint8_t *mask = p++;
                *mask = 0x80;
                if (cell->note) { *mask |= 3; *p++ = cell->note; *p++ = 1; }
                if (cell->fx || cell->param) { *mask |= 24; *p++ = cell->fx; *p++ = cell->param; }
                if (cell->ext) { *mask |= 32; *p++ = cell->ext; *p++ = cell->extParam; *p++ = cell->extParam >> 8; }
            }
        }
        struct XMPatternEntry *pat = (struct XMPatternEntry *)(s_file + PATTERNS);
        pat->offset = DATA;
        pat->rows = ROWS;
        pat->size = p - (s_file + DATA);
        return XM_Load(s_file, NULL);
    }

    static void poll(unsigned count) {
        for (unsigned i = 0; i < count; i++) XM_Poll();
    }

    static uint32_t distance(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }
)

CESTER_TEST(xmplayer_check_versions, test_instance,
    // Version 1 with its 64 bytes header and version 2 with 72 are accepted;
    // a version 2 header shorter than 72 bytes, and version 3, are not.
    buildSong(1, 0, 0);
    struct XMSongHeader *h = (struct XMSongHeader *)s_file;
    unsigned v1 = XM_Check(s_file);
    h->version = 2;
    h->headerSize = 72;
    unsigned v2 = XM_Check(s_file);
    h->headerSize = 64;
    unsigned v2short = XM_Check(s_file);
    h->headerSize = 72;
    h->version = 3;
    unsigned v3 = XM_Check(s_file);
    ramsyscall_printf("check: v1 %u, v2 %u, v2 header 64 %u, v3 %u\n", v1, v2, v2short, v3);
    cester_assert_uint_eq(2, v1);
    cester_assert_uint_eq(2, v2);
    cester_assert_uint_eq(0, v2short);
    cester_assert_uint_eq(0, v3);
)

CESTER_TEST(xmplayer_hw_adsr, test_instance,
    // A flags bit 2 sample keys on with its adsr word in ADSR1 and ADSR2; a
    // sample without it puts back the player's default, 0x000f / 0x0000, on a
    // voice that had the hardware envelope.
    buildSong(2, 0, 0);
    setCell(0, 0, HWADSR, 0, 0, 0, 0);
    setCell(0, 1, HWADSR, 0, 0, 0, 0);
    setCell(1, 1, PLAIN, 0, 0, 0, 0);
    load();
    poll(1);
    uint16_t lo0 = SPU_VOICES[0].adsrLo, hi0 = SPU_VOICES[0].adsrHi;
    uint16_t lo1a = SPU_VOICES[1].adsrLo, hi1a = SPU_VOICES[1].adsrHi;
    poll(1);
    uint16_t lo1 = SPU_VOICES[1].adsrLo, hi1 = SPU_VOICES[1].adsrHi;
    ramsyscall_printf("hw_adsr: voice 0 %04x %04x, voice 1 row 0 %04x %04x, row 1 %04x %04x\n", lo0, hi0, lo1a, hi1a, lo1, hi1);
    // cester pastes the operands into the comparison unparenthesized, so
    // expressions go in locals first
    uint16_t adsrLo = ADSR & 0xffff, adsrHi = ADSR >> 16;
    cester_assert_uint_eq(adsrLo, lo0);
    cester_assert_uint_eq(adsrHi, hi0);
    cester_assert_uint_eq(adsrLo, lo1a);
    cester_assert_uint_eq(adsrHi, hi1a);
    cester_assert_uint_eq(0x000f, lo1);
    cester_assert_uint_eq(0x0000, hi1);
)

CESTER_TEST(xmplayer_v1_ignores_v2_flags, test_instance,
    // In a version 1 file, sample flag bits 2 to 5 are ignored: the hardware
    // ADSR sample keys on with the default ADSR, the reverb and noise samples
    // set no mask bit, and a note off on the hardware ADSR sample keys
    // nothing off.
    buildSong(1, 0, 0);
    setCell(0, 0, HWADSR, 0, 0, 0, 0);
    setCell(0, 1, VERB, 0, 0, 0, 0);
    setCell(1, 1, NOISE, 0, 0, 0, 0);
    setCell(2, 0, NOTE_OFF, 0, 0, 0, 0);
    load();
    poll(1);
    uint16_t lo = SPU_VOICES[0].adsrLo, hi = SPU_VOICES[0].adsrHi;
    uint32_t verb = s_reverbMask;
    poll(1);
    uint32_t noise = s_noiseMask;
    poll(1);
    uint32_t off = s_keyOff;
    ramsyscall_printf("v1 flags: adsr %04x %04x, reverb %x, noise %x, key off %x\n", lo, hi, verb, noise, off);
    cester_assert_uint_eq(0x000f, lo);
    cester_assert_uint_eq(0x0000, hi);
    cester_assert_uint_eq(0, verb);
    cester_assert_uint_eq(0, noise);
    cester_assert_uint_eq(0, off);
)

CESTER_TEST(xmplayer_voice_masks, test_instance,
    // Flags bits 3, 4 and 5 set the voice's reverb, noise and pitch modulation
    // bits, and a key on of a sample without them clears them. Voice 0 has no
    // voice below it, so it never gets pitch modulation.
    buildSong(2, 0, 0);
    setCell(0, 0, VERB, 0, 0, 0, 0);
    setCell(0, 1, NOISE, 0, 0, 0, 0);
    setCell(1, 0, PMOD, 0, 0, 0, 0);
    setCell(1, 1, PMOD, 0, 0, 0, 0);
    load();
    poll(1);
    uint32_t verb0 = s_reverbMask, noise0 = s_noiseMask, pmod0 = s_pmodMask;
    uint16_t verbReg0 = SPU_REVERB_EN_LOW, noiseReg0 = SPU_NOISE_EN_LOW, pmodReg0 = SPU_PITCH_MOD_LOW;
    poll(1);
    uint32_t verb1 = s_reverbMask, noise1 = s_noiseMask, pmod1 = s_pmodMask;
    uint16_t verbReg1 = SPU_REVERB_EN_LOW, noiseReg1 = SPU_NOISE_EN_LOW, pmodReg1 = SPU_PITCH_MOD_LOW;
    ramsyscall_printf("masks row 0: reverb %x/%x noise %x/%x pmod %x/%x\n", verb0, verbReg0, noise0, noiseReg0, pmod0, pmodReg0);
    ramsyscall_printf("masks row 1: reverb %x/%x noise %x/%x pmod %x/%x\n", verb1, verbReg1, noise1, noiseReg1, pmod1, pmodReg1);
    cester_assert_uint_eq(1, verb0);
    cester_assert_uint_eq(2, noise0);
    cester_assert_uint_eq(0, pmod0);
    cester_assert_uint_eq(1, verbReg0);
    cester_assert_uint_eq(2, noiseReg0);
    cester_assert_uint_eq(0, pmodReg0);
    cester_assert_uint_eq(0, verb1);
    cester_assert_uint_eq(0, noise1);
    cester_assert_uint_eq(2, pmod1);
    cester_assert_uint_eq(0, verbReg1);
    cester_assert_uint_eq(0, noiseReg1);
    cester_assert_uint_eq(2, pmodReg1);
)

CESTER_TEST(xmplayer_reverb_block, test_instance,
    // A reverb block sets mBASE and the reverb volumes and turns the reverb
    // unit on, SPUCNT bit 7. Without one, or in a version 1 file whatever its
    // reverbOffset field holds, it stays off and its output is muted, since
    // the previous song's reverb may still be in the work area.
    buildSong(2, 0, 1);
    load();
    uint16_t ctrl = s_spuCtrl, ctrlReg = SPU_CTRL, base = SPU_REVERB_ADDR;
    uint16_t left = SPU_REVERB_LEFT, right = SPU_REVERB_RIGHT, vlin = SPU_REVERB->vLIN;
    buildSong(2, 0, 0);
    load();
    uint16_t ctrlOff = s_spuCtrl, ctrlRegOff = SPU_CTRL, leftOff = SPU_REVERB_LEFT, rightOff = SPU_REVERB_RIGHT;
    buildSong(1, 0, 1);
    load();
    uint16_t ctrlV1 = s_spuCtrl;
    ramsyscall_printf("reverb: ctrl %04x reg %04x base %04x vol %04x %04x vLIN %04x; off ctrl %04x reg %04x; v1 ctrl %04x\n",
                      ctrl, ctrlReg, base, left, right, vlin, ctrlOff, ctrlRegOff, ctrlV1);
    cester_assert_uint_eq(0xc080, ctrl);
    unsigned enabled = ctrlReg & 0x80, enabledOff = ctrlRegOff & 0x80;
    cester_assert_uint_eq(0x80, enabled);
    cester_assert_uint_eq(0xfe00, base);
    cester_assert_uint_eq(0x1000, left);
    cester_assert_uint_eq(0x0800, right);
    cester_assert_uint_eq(0x2000, vlin);
    cester_assert_uint_eq(0xc000, ctrlOff);
    cester_assert_uint_eq(0, enabledOff);
    cester_assert_uint_eq(0, leftOff);
    cester_assert_uint_eq(0, rightOff);
    cester_assert_uint_eq(0xc000, ctrlV1);
)

CESTER_TEST(xmplayer_levels_and_noise_clock, test_instance,
    // Extended effect 1 sets left 255, right 0: the volume is 4096 before the
    // sides, so left is 4096 * 255 >> 6 = 16320, then times the default music
    // volume 16384 >> 16 = 4080, and right is 0. 880 on the next row goes
    // back to panning, centre: 4096 * 16384 >> 13 >> 2 = 2048 on each side.
    // Extended effect 2 sets the noise clock, SPUCNT bits 13-8.
    buildSong(2, 0, 0);
    setCell(0, 0, PLAIN, 0, 0, 1, 0x00ff);
    setCell(1, 0, 0, 8, 0x80, 0, 0);
    setCell(2, 0, 0, 0, 0, 2, 0x15);
    load();
    poll(1);
    unsigned mode0 = s_channels[0].levelMode;
    uint16_t left0 = SPU_VOICES[0].volumeLeft, right0 = SPU_VOICES[0].volumeRight;
    poll(1);
    unsigned mode1 = s_channels[0].levelMode;
    uint16_t left1 = SPU_VOICES[0].volumeLeft, right1 = SPU_VOICES[0].volumeRight;
    poll(1);
    unsigned clock = (s_spuCtrl >> 8) & 63;
    uint16_t ctrlReg = SPU_CTRL;
    ramsyscall_printf("levels: row 0 mode %u vol %04x %04x, row 1 mode %u vol %04x %04x; noise clock %x ctrl %04x reg %04x\n",
                      mode0, left0, right0, mode1, left1, right1, clock, s_spuCtrl, ctrlReg);
    cester_assert_uint_eq(1, mode0);
    cester_assert_uint_eq(4080, left0);
    cester_assert_uint_eq(0, right0);
    cester_assert_uint_eq(0, mode1);
    cester_assert_uint_eq(2048, left1);
    cester_assert_uint_eq(2048, right1);
    cester_assert_uint_eq(0x15, clock);
    cester_assert_uint_eq(0xd500, s_spuCtrl);
    cester_assert_uint_eq(0xd500, s_writtenCtrl);
    cester_assert_uint_eq(0xd500, ctrlReg);
)

CESTER_TEST(xmplayer_hw_adsr_key_off, test_instance,
    // A note off on a hardware ADSR voice keys it off for that poll and keeps
    // the channel volume, the release does the fading. On a plain sample
    // without a volume envelope it zeroes the volume and keys nothing off.
    buildSong(2, 0, 0);
    setCell(0, 0, HWADSR, 0, 0, 0, 0);
    setCell(0, 1, PLAIN, 0, 0, 0, 0);
    setCell(1, 0, NOTE_OFF, 0, 0, 0, 0);
    setCell(1, 1, NOTE_OFF, 0, 0, 0, 0);
    load();
    poll(1);
    uint32_t off0 = s_keyOff;
    poll(1);
    uint32_t off1 = s_keyOff;
    int vol0 = s_channels[0].volume, vol1 = s_channels[1].volume;
    poll(1);
    uint32_t off2 = s_keyOff;
    ramsyscall_printf("key off: row 0 %x, row 1 %x (volumes %d %d), row 2 %x\n", off0, off1, vol0, vol1, off2);
    cester_assert_uint_eq(0, off0);
    cester_assert_uint_eq(1, off1);
    cester_assert_int_eq(64, vol0);
    cester_assert_int_eq(0, vol1);
    cester_assert_uint_eq(0, off2);
)

CESTER_TEST(xmplayer_hw_adsr_k00_after_note, test_instance,
    // A hardware ADSR note and K00 in the same cell: the note keys on, and its
    // key off goes out on the next poll so the voice releases instead of
    // sustaining. A note on the next row cancels a pending key off.
    buildSong(2, 0, 0);
    setCell(0, 0, HWADSR, 20, 0, 0, 0);
    setCell(2, 0, HWADSR, 20, 0, 0, 0);
    setCell(3, 0, HWADSR, 0, 0, 0, 0);
    load();
    poll(1);
    uint32_t on0 = s_keyOn, off0 = s_keyOff;
    poll(1);
    uint32_t on1 = s_keyOn, off1 = s_keyOff;
    poll(1);
    uint32_t next2 = s_keyOffNext;
    poll(1);
    uint32_t on3 = s_keyOn, off3 = s_keyOff;
    ramsyscall_printf("k00: row 0 on %x off %x, row 1 on %x off %x, row 2 next %x, row 3 on %x off %x\n",
                      on0, off0, on1, off1, next2, on3, off3);
    cester_assert_uint_eq(1, on0);
    cester_assert_uint_eq(0, off0);
    cester_assert_uint_eq(0, on1);
    cester_assert_uint_eq(1, off1);
    cester_assert_uint_eq(1, next2);
    cester_assert_uint_eq(1, on3);
    cester_assert_uint_eq(0, off3);
)

CESTER_TEST(xmplayer_tick_rate, test_instance,
    // tickRate 0 is bpm * 2 / 5 ticks per second, as in version 1; that rate
    // given exactly, 125 * 2 * 65536 / 5, gives the same step within 1, and
    // twice it about half the step. Fxx scales an explicit rate with
    // bpm / initial bpm, here 250 / 125, so it halves the step too. The
    // version 1 file holds the doubled rate in its tickRate field, which the
    // player must ignore.
    uint32_t rate = 125 * 2 * 65536 / 5;
    buildSong(1, rate * 2, 0);
    load();
    uint32_t v1 = s_hblankStep;
    buildSong(2, 0, 0);
    load();
    uint32_t zero = s_hblankStep;
    buildSong(2, rate, 0);
    load();
    uint32_t exact = s_hblankStep;
    buildSong(2, rate * 2, 0);
    load();
    uint32_t doubled = s_hblankStep;
    buildSong(2, rate, 0);
    setCell(0, 0, 0, 15, 250, 0, 0);
    load();
    poll(1);
    uint32_t fxx = s_hblankStep;
    unsigned bpm = XM_BPM;
    uint32_t dExact = distance(exact, zero), dDoubled = distance(doubled, zero >> 1), dFxx = distance(fxx, zero >> 1);
    ramsyscall_printf("tick rate: v1 %u, v2 rate 0 %u, exact %u, doubled %u, F%02x %u (bpm %u)\n", v1, zero, exact, doubled, 250, fxx, bpm);
    cester_assert_uint_eq(v1, zero);
    cester_assert_uint_le(dExact, 1);
    cester_assert_uint_le(dDoubled, 1);
    cester_assert_uint_eq(250, bpm);
    cester_assert_uint_le(dFxx, 1);
)
