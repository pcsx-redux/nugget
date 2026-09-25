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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"

// vasprintf in common/libc/xprintf.c copies with __builtin_strcpy, which
// becomes a call, and nothing else here provides one.
char *strcpy(char *dst, const char *src) {
    char *r = dst;
    while ((*dst++ = *src++));
    return r;
}

struct FILE {
    int fd;
};

static FILE s_stdout = {1};
static FILE s_stderr = {2};
FILE *stdout = &s_stdout;
FILE *stderr = &s_stderr;

static void ttyOut(const char *str, int size, void *opaque) {
    while (size--) syscall_putchar(*str++);
}

int vdprintf(int fd, const char *fmt, va_list ap) { return vxprintf(ttyOut, NULL, fmt, ap); }
int vfprintf(FILE *stream, const char *fmt, va_list ap) { return vdprintf(stream->fd, fmt, ap); }

// Trap into the resident debugger with the exit code in $a0 via break
// category 4. Categories 0/6/7/14 are taken (pcdrv / compiler overflow /
// compiler divide-by-zero / psyqo), so 4 is free. On hardware this halts
// the program (Unirom reports HLTD) and leaves the exit code readable in
// $a0, giving the host a deterministic end-of-binary signal instead of a
// printed sentinel string. On the emulator pcsx_exit() has already exited,
// so this is never reached there.
static inline void exitBreak(int code) {
    register int a0 asm("$4") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0) : "memory");
}

void exit(int code) {
    pcsx_exit(code);
    exitBreak(code);
    syscall__exit(code);
    while (1);
}

typedef void (*fptr)();
extern fptr __preinit_array_start[] __attribute__((weak));
extern fptr __preinit_array_end[] __attribute__((weak));
extern fptr __init_array_start[] __attribute__((weak));
extern fptr __init_array_end[] __attribute__((weak));

int main(int argc, char **argv);

// Entry point from crt0.s, once the bss is cleared.
void tests_start() {
    for (fptr *f = __preinit_array_start; f < __preinit_array_end; f++) {
        if (*f) (*f)();
    }
    for (fptr *f = __init_array_start; f < __init_array_end; f++) {
        if (*f) (*f)();
    }
    exit(main(0, NULL));
}
