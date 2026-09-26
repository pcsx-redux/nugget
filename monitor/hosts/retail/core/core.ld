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

/* The resident half of the retail monitor, linked into the free RAM between
   the retail kernel's bss (ends at 0xC160) and the BIOS patch area games use
   at 0xDF80; 0xE000 up is the kernel's control-block pool. bss is folded into
   the image as zeroes, so copying the image is the whole install and there is
   no crt0. The loader links against this ELF for the symbols it needs. */

EXTERN(s_monitorHandler)
EXTERN(s_ttyDevice)

MEMORY {
    cave (rwx) : ORIGIN = 0x8000C160, LENGTH = 0x8000DF80 - 0x8000C160
}

SECTIONS {
    .text : {
        __core_start = .;
        *(.text .text.*)
    } > cave
    .rodata : {
        *(.rodata .rodata.* .rdata .rdata.*)
    } > cave
    .data : {
        *(.data .data.* .sdata .sdata.*)
        *(.sbss .sbss.* .scommon .bss .bss.* COMMON)
        . = ALIGN(4);
        __core_end = .;
        __bss_end = .; /* nooverlay.ld wants it */
    } > cave

    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.note.GNU-stack) *(.gnu.lto_*) *(.comment) }
}
