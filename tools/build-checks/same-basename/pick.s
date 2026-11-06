    .section .text.pick_from_s, "ax", @progbits
    .global pick_from_s
    .type pick_from_s, @function
pick_from_s:
    jr    $ra
    li    $v0, 2
