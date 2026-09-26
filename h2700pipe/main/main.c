/* Full v1 monitor pipeline target: tty print, one PCDRV file, then an exit
   break with code 42. Loads at 0x80010000, below the RAM-linked monitor. */
#include "common/kernel/pcdrv.h"
#include "common/syscalls/syscalls.h"

int main(void) {
    volatile unsigned int *marker = (volatile unsigned int *)0x1f800000u;
    marker[0] = 0x71BE7E57u;
    syscall_puts("target: hello over the monitor tty\n");
    int fd = PCcreat("pipe-out.txt", 0);
    marker[1] = (unsigned int)fd;
    if (fd >= 0) {
        static const char msg[] = "written by the target through PCDRV\n";
        int n = PCwrite(fd, msg, sizeof(msg) - 1);
        marker[2] = (unsigned int)n;
        PCclose(fd);
    }
    syscall_puts("target: exiting with 42\n");
    register int a0 asm("a0") = 42;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    for (;;) {}
    return 0;
}
