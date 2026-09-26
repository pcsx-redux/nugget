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


#pragma once

#include <stdint.h>

#include "common/kernel/threads.h"

/* The monitor reaches the kernel only through what the retail BIOS kernel also
   provides: the table of tables at 0x100, the RAM size word at 0x60, and the
   A0/B0/C0 calls in common/syscalls. OpenBIOS lays these out identically, so the
   same monitor runs on either. __globals and __globals60 are the host's linker
   symbols for those two addresses, and psxprintf is the host's printf. */

extern struct {
    uint32_t ramsize;
} __globals60;

extern struct {
    void *handlersArray;
    uint32_t handlersArraySize;
    struct Process *processes;
} __globals;

int psxprintf(const char *msg, ...);
