/* Minimal crt0 for the RAM-loaded HELLO transport probe.
 *
 * h2x00-cpe-player loads this at 0x80100000 and sets PC = _start. The psyqo
 * crt0 (crt0cxx.s) never sets $sp - it relies on the BIOS EXE loader to poke
 * the stack from the EXE header before entry. We bypass the BIOS entirely
 * (raw DECI load + PC set), so set a safe stack ourselves. 0x801FFF00 is
 * valid on 2 MiB retail and 8 MiB devkit alike, and ~1 MiB clear of the probe
 * image at 0x80100000. Built -G0, so no $gp setup is needed. */
    .set noreorder
    .section .text.start, "ax", @progbits
    .global _start
    .type _start, @function
_start:
    lui   $sp, 0x801F
    ori   $sp, $sp, 0xFF00
    jal   probeMain
    nop
_hang:
    b     _hang
    nop
    .size _start, .-_start
