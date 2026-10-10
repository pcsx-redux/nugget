# dev8-readback results

SCPH-1000, SCPH-1001 and SCPH-7001: DEV8 (1F80101Ch) is 00080777h at entry,
and 00070777h, 00080777h, 00000777h and 00010777h each read back unchanged,
twice. The BIOS reset code writes 00070777h (SCPH-5501 ROM, BFC001A8h), so
something later in the boot sets the size field to 8.

An earlier version of this test went on to write 000F0777h and stopped there
on all three consoles, with no further output. Not investigated.
