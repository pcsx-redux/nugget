#include <stdint.h>
extern const int16_t _binary_s0_raw_start[], _binary_s0_raw_end[];
extern const int16_t _binary_s1_raw_start[], _binary_s1_raw_end[];
extern const int16_t _binary_s4_raw_start[], _binary_s4_raw_end[];
void adpcmEncodeBlock(const int16_t* in, uint8_t* out, int32_t h[2]);

#define T1_VALUE (*(volatile uint32_t*)0x1f801110)
#define T1_MODE (*(volatile uint32_t*)0x1f801114)

static uint8_t s_out[(47740 + 23884 + 13020) / 28 * 16 + 64];
/* Read by the Lua harness: magic, hblanks per sample buffer, output pointer, sizes. */
volatile uint32_t g_result[8] = {0x41444543};

static unsigned now(unsigned* last, unsigned* acc) {
    unsigned v = T1_VALUE & 0xffff;
    *acc += (v - *last) & 0xffff;
    *last = v;
    return *acc;
}

int main() {
    T1_MODE = 0x100; /* hblank clock, free running */
    const int16_t* starts[3] = {_binary_s0_raw_start, _binary_s1_raw_start, _binary_s4_raw_start};
    const int16_t* ends[3] = {_binary_s0_raw_end, _binary_s1_raw_end, _binary_s4_raw_end};
    uint8_t* o = s_out;
    unsigned last = T1_VALUE & 0xffff, acc = 0;
    for (unsigned i = 0; i < 3; i++) {
        unsigned blocks = (ends[i] - starts[i]) / 28;
        int32_t h[2] = {0, 0};
        unsigned t0 = now(&last, &acc);
        /* the encoder runs in chunks short enough that timer 1 cannot wrap twice between reads */
        for (unsigned b = 0; b < blocks; b += 64) {
            unsigned n = blocks - b < 64 ? blocks - b : 64;
            for (unsigned k = 0; k < n; k++) adpcmEncodeBlock(starts[i] + 28 * (b + k), o + 16 * (b + k), h);
            now(&last, &acc);
        }
        g_result[1 + i] = now(&last, &acc) - t0;
        o += 16 * blocks;
    }
    g_result[4] = (uint32_t)s_out;
    g_result[5] = o - s_out;
    g_result[0] = 0x454e4f44;
    while (1);
}
