/* Per-word arithmetic cost of checksum candidates for the H2x00 monitor
   transport, timed with root counter 2 on sysclk, IRQs masked, min of 8.
   The word is already in a register when the transport folds it in, so the
   baseline arm (load + xor) is subtracted to get the arithmetic alone. */
#include <stdint.h>
#include "common/syscalls/syscalls.h"

#define RCNT2_VALUE (*(volatile uint16_t *)0x1f801120)
#define RCNT2_MODE  (*(volatile uint16_t *)0x1f801124)
#define N 512

static uint16_t s_buf[N];
static volatile uint32_t s_sink;
static uint32_t s_nib[16];
static uint32_t s_tab[256];

static char *putdec(char *p, uint32_t v) {
    char t[12]; int i = 0;
    do { t[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *p++ = t[--i];
    return p;
}
static char *putstr(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

__attribute__((noinline)) static uint32_t arm_base(const uint16_t *b) {
    uint32_t x = 0;
    for (int i = 0; i < N; i++) x ^= b[i];
    return x;
}
__attribute__((noinline)) static uint32_t arm_djb2(const uint16_t *b) {
    uint32_t h = 5381;
    for (int i = 0; i < N; i++) { uint16_t w = b[i]; h = h * 33 + (w & 0xff); h = h * 33 + (w >> 8); }
    return h;
}
__attribute__((noinline)) static uint32_t arm_fletcher(const uint16_t *b) {
    uint32_t s1 = 0, s2 = 0;
    for (int i = 0; i < N; i++) { s1 += b[i]; s2 += s1; }
    s1 %= 65535; s2 %= 65535;
    return (s2 << 16) | s1;
}
__attribute__((noinline)) static uint32_t arm_adler(const uint16_t *b) {
    uint32_t a = 1, s = 0;
    for (int i = 0; i < N; i++) { uint16_t w = b[i]; a += w & 0xff; s += a; a += w >> 8; s += a; }
    a %= 65521; s %= 65521;
    return (s << 16) | a;
}
__attribute__((noinline)) static uint32_t arm_crcbit(const uint16_t *b) {
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < N; i++) {
        c ^= b[i];
        for (int k = 0; k < 16; k++) c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}
__attribute__((noinline)) static uint32_t arm_crcnib(const uint16_t *b) {
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < N; i++) {
        c ^= b[i];
        c = s_nib[c & 15] ^ (c >> 4); c = s_nib[c & 15] ^ (c >> 4);
        c = s_nib[c & 15] ^ (c >> 4); c = s_nib[c & 15] ^ (c >> 4);
    }
    return ~c;
}
__attribute__((noinline)) static uint32_t arm_crctab(const uint16_t *b) {
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < N; i++) {
        uint16_t w = b[i];
        c = s_tab[(c ^ (w & 0xff)) & 0xff] ^ (c >> 8);
        c = s_tab[(c ^ (w >> 8)) & 0xff] ^ (c >> 8);
    }
    return ~c;
}

static uint32_t timeit(uint32_t (*fn)(const uint16_t *)) {
    uint32_t best = 0xffffffffu;
    for (int r = 0; r < 8; r++) {
        uint16_t t0 = RCNT2_VALUE;
        s_sink = fn(s_buf);
        uint16_t t1 = RCNT2_VALUE;
        uint32_t d = (uint16_t)(t1 - t0);
        if (d < best) best = d;
    }
    return best;
}
static void report(const char *name, uint32_t cyc, uint32_t base) {
    char line[96], *p = line;
    p = putstr(p, name); p = putstr(p, ": "); p = putdec(p, cyc); p = putstr(p, " cyc / ");
    p = putdec(p, N); p = putstr(p, " words = "); p = putdec(p, (cyc * 100) / N);
    p = putstr(p, " (x0.01/word), marginal over baseline ");
    p = putdec(p, cyc > base ? ((cyc - base) * 100) / N : 0); p = putstr(p, "\n"); *p = 0;
    syscall_puts(line);
}

int main(void) {
    uint32_t lcg = 0x12345678u;
    for (int i = 0; i < N; i++) { lcg = lcg * 1664525u + 1013904223u; s_buf[i] = lcg >> 16; }
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & -(c & 1));
        s_tab[n] = c;
        if (n < 16) { uint32_t d = n; for (int k = 0; k < 4; k++) d = (d >> 1) ^ (0xedb88320u & -(d & 1)); s_nib[n] = d; }
    }
    syscall_puts("=== CKSUM ARITHMETIC, RCNT2 sysclk, N=512 words, min of 8 ===\n");
    enterCriticalSection();
    RCNT2_MODE = 0; /* free-running, sysclk */
    uint32_t base = timeit(arm_base);
    uint32_t djb2 = timeit(arm_djb2);
    uint32_t flet = timeit(arm_fletcher);
    uint32_t adlr = timeit(arm_adler);
    uint32_t crcb = timeit(arm_crcbit);
    uint32_t crcn = timeit(arm_crcnib);
    uint32_t crct = timeit(arm_crctab);
    uint32_t base2 = timeit(arm_base);
    leaveCriticalSection();
    report("baseline   ", base, base);
    report("djb2       ", djb2, base);
    report("fletcher32 ", flet, base);
    report("adler32    ", adlr, base);
    report("crc32-bit  ", crcb, base);
    report("crc32-nib  ", crcn, base);
    report("crc32-tab  ", crct, base);
    report("baseline2  ", base2, base);
    syscall_puts("=== END ===\n");
    return 0;
}
