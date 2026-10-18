#include <stdint.h>

/* SPU ADPCM block encoder: 28 samples in, 16 bytes out. The filter is the
   one with the smallest open-loop prediction peak, the shift is computed
   from that peak, then one closed-loop quantisation pass. h holds the last
   two decoded samples across calls. Flags byte left 0. Each filter gets its
   own loop with constant coefficients, so the multiplies become shifts. */
#define PEAK(F0, F1)                                                  \
    do {                                                              \
        int32_t a1 = h[0], a2 = h[1];                                 \
        unsigned peak = 0;                                            \
        for (unsigned i = 0; i < 28; i++) {                           \
            int32_t s = in[i];                                        \
            int32_t r = s - ((a1 * (F0) + a2 * (F1) + 32) >> 6);      \
            unsigned m = r < 0 ? -r : r;                              \
            if (m > peak) peak = m;                                   \
            a2 = a1;                                                  \
            a1 = s;                                                   \
        }                                                             \
        peaks[f++] = peak;                                            \
    } while (0)

#define QUANT(F0, F1)                                                 \
    do {                                                              \
        int32_t a1 = h[0], a2 = h[1];                                 \
        for (unsigned i = 0; i < 28; i++) {                           \
            int32_t p = (a1 * (F0) + a2 * (F1) + 32) >> 6;            \
            int32_t q = (((in[i] - p) << sh) + (1 << 11)) >> 12;      \
            if (q > 7) q = 7;                                         \
            if (q < -8) q = -8;                                       \
            int32_t v = ((q << 12) >> sh) + p;                        \
            if (v > 32767) v = 32767;                                 \
            if (v < -32768) v = -32768;                               \
            a2 = a1;                                                  \
            a1 = v;                                                   \
            nib[i] = q & 15;                                          \
        }                                                             \
        h[0] = a1;                                                    \
        h[1] = a2;                                                    \
    } while (0)

void adpcmEncodeBlock(const int16_t* in, uint8_t* out, int32_t h[2]) {
    unsigned peaks[5], f = 0;
    PEAK(0, 0);
    PEAK(60, 0);
    PEAK(115, -52);
    PEAK(98, -55);
    PEAK(122, -60);
    unsigned best = 0;
    for (f = 1; f < 5; f++)
        if (peaks[f] < peaks[best]) best = f;
    unsigned sh = 12;
    while (sh > 0 && (peaks[best] << sh) > (7 << 12)) sh--;
    uint8_t nib[28];
    switch (best) {
        case 0: QUANT(0, 0); break;
        case 1: QUANT(60, 0); break;
        case 2: QUANT(115, -52); break;
        case 3: QUANT(98, -55); break;
        default: QUANT(122, -60); break;
    }
    out[0] = (best << 4) | sh;
    out[1] = 0;
    for (unsigned i = 0; i < 14; i++) out[2 + i] = nib[2 * i] | (nib[2 * i + 1] << 4);
}
