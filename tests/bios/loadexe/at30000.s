    .set noreorder

# The binary is linked at 0x80030000, which is where OpenBIOS runs its shell
# from. When using -loadexe, the shell is hijacked at its entry point, so a
# dynarec block for that address already exists by the time this gets loaded.
# Calling this function has to run the code below, not the shell's.
    .section .start, "ax", @progbits
    .align 2
    .global at30000
    .type at30000, @function
at30000:
    jr    $ra
    li    $v0, 0x1234

# If a stale block from the shell runs instead and falls through past the
# first instructions, land somewhere that returns a recognizable bad value.
    .rept 13
    jr    $ra
    li    $v0, 0xbad
    .endr
