/* Dump the H2700's as-found memory-control block over the ATCONS tty, before
   anything of ours rewrites it. Same puts-over-ATCONS pattern as the June
   oracles. Run from main RAM under the stock DECI boot. */
#include "common/syscalls/syscalls.h"

static __attribute__((always_inline)) void installStdIo_(int installTTY) {
    register int n asm("t1") = 0x1b;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void (*)(int))0xc0)(installTTY);
}
static char *puthex(char *p, unsigned int v, int digits) {
    static const char h[] = "0123456789ABCDEF";
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) *p++ = h[(v >> i) & 0xFu];
    return p;
}
static char *putstr(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static void line32(const char *name, unsigned int addr) {
    char buf[64], *p = buf;
    unsigned int v = *(volatile unsigned int *)addr;
    p = putstr(p, name); p = putstr(p, " @"); p = puthex(p, addr, 8); p = putstr(p, " = ");
    p = puthex(p, v, 8); *p++ = '\n'; *p = 0; syscall_puts(buf);
}
static void line8(const char *name, unsigned int addr) {
    char buf[64], *p = buf;
    unsigned int v = *(volatile unsigned char *)addr;
    p = putstr(p, name); p = putstr(p, " @"); p = puthex(p, addr, 8); p = putstr(p, " = ");
    p = puthex(p, v, 2); *p++ = '\n'; *p = 0; syscall_puts(buf);
}
int main(void) {
    volatile unsigned int *marker = (volatile unsigned int *)0x1f800000u;
    marker[0] = 0x3E3C7717u;
    installStdIo_(1);
    syscall_puts("=== MEMCTL AS FOUND ===\n");
    line32("DEV0_ADDR ", 0x1f801000u);
    line32("DEV8_ADDR ", 0x1f801004u);
    line32("DEV0_CTRL ", 0x1f801008u);
    line32("DEV1_CTRL ", 0x1f80100cu);
    line32("DEV2_CTRL ", 0x1f801010u);
    line32("DEV4_CTRL ", 0x1f801014u);
    line32("DEV5_CTRL ", 0x1f801018u);
    line32("DEV8_CTRL ", 0x1f80101cu);
    line32("COM_CTRL  ", 0x1f801020u);
    line32("RAM_SIZE  ", 0x1f801060u);
    line32("g60ramsize", 0x00000060u);
    line8 ("DIPSW     ", 0x1f802040u);
    line8 ("ATCONS_ST ", 0x1f802000u);
    line8 ("ATCONS_IRQ", 0x1f802030u);
    line8 ("ATCONS_IQ2", 0x1f802032u);
    syscall_puts("=== END ===\n");
    for (;;) {}
    return 0;
}
