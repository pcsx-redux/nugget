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
#ifndef MODPLAYER_STATE_PLAYER
#define MODPLAYER_STATE_PLAYER
#include "common/syscalls/syscalls.h"
#include "modplayer/modplayer.c"
#endif

#undef unix
#define CESTER_NO_SIGNAL
#define CESTER_NO_TIME
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include "exotic/cester.h"

// clang-format off

CESTER_BODY(
    // A 4 channels .hit module with one pattern, built in memory. Samples are
    // one silent looped block each; the tests only read back the player state.
    static uint8_t s_module[sizeof(struct MODFileFormat) + 4 * 0x100 + 16] __attribute__((aligned(4)));

    static uint8_t *cell(unsigned row, unsigned channel) {
        return s_module + sizeof(struct MODFileFormat) + (row * 4 + channel) * 4;
    }

    static void setCell(unsigned row, unsigned channel, unsigned sample, unsigned period, unsigned fx, unsigned param) {
        uint8_t *c = cell(row, channel);
        c[0] = (sample & 0xf0) | (period >> 8);
        c[1] = period & 0xff;
        c[2] = ((sample & 0x0f) << 4) | fx;
        c[3] = param;
    }

    static void buildModule() {
        __builtin_memset(s_module, 0, sizeof(s_module));
        struct MODFileFormat *m = (struct MODFileFormat *)s_module;
        for (unsigned i = 0; i < 2; i++) {
            m->samples[i].lenarr[1] = 16;
            m->samples[i].volume = 64;
            m->samples[i].repeatLength = 1;
        }
        m->songLength = 1;
        m->signature[0] = 'H';
        m->signature[1] = 'I';
        m->signature[2] = 'T';
        m->signature[3] = '4';
    }

    // Runs the player until it has processed the given row: the first poll
    // plays row 0, then each row takes 6 more at the default speed.
    static void playThroughRow(unsigned row) {
        MOD_LoadEx((const struct MODFileFormat *)s_module, (const uint8_t *)-1);
        for (unsigned i = 0; i < row * 6 + 1; i++) MOD_Poll();
    }
)

CESTER_TEST(modplayer_lookup_b3, test_instance,
    // B-3, the last note of the table, period 113.
    buildModule();
    setCell(0, 0, 1, 113, 0, 0);
    playThroughRow(0);
    cester_assert_uint_eq(35, s_channelData[0].note);
    cester_assert_int_eq(113, s_channelData[0].period);
)

CESTER_TEST(modplayer_lookup_c1, test_instance,
    // C-1, the first note of the table, period 856.
    buildModule();
    setCell(0, 0, 1, 856, 0, 0);
    playThroughRow(0);
    cester_assert_uint_eq(0, s_channelData[0].note);
    cester_assert_int_eq(856, s_channelData[0].period);
)

CESTER_TEST(modplayer_e5x_current_sample, test_instance,
    // E5x on a row without a sample number sets the finetune of the
    // channel's current sample, here sample 2, and not sample 1's.
    buildModule();
    setCell(0, 0, 2, 428, 0, 0);
    setCell(1, 0, 0, 0, 0xe, 0x53);
    playThroughRow(1);
    cester_assert_uint_eq(3, s_spuInstrumentData[1].finetune);
    cester_assert_uint_eq(0, s_spuInstrumentData[0].finetune);
)

CESTER_TEST(modplayer_e5x_row_sample, test_instance,
    // E5x on a row with a sample number sets that sample's finetune.
    buildModule();
    setCell(0, 0, 2, 428, 0xe, 0x5d);
    playThroughRow(0);
    cester_assert_uint_eq(13, s_spuInstrumentData[1].finetune);
    cester_assert_uint_eq(0, s_spuInstrumentData[0].finetune);
)
