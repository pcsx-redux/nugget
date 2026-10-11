# dev8-readback results

SCPH-1000, SCPH-1001 and SCPH-7001: 00070777h, 00080777h, 00000777h and
00010777h each read back unchanged from DEV8 (1F80101Ch), twice.

The entry value printed is 00080777h, but that is set by this suite's own
startup code: tests/support/crt0.s raises the size field to at least 8 before
main. The BIOS reset code writes 00070777h.

An earlier version of this test went on to write 000F0777h and stopped there
on all three consoles, with no further output. Not investigated.
