    .section .rodata.payload, "a", @progbits
    .align 4
    .global g_payload
g_payload:
    .incbin "image.bin"
    .global g_payload_end
g_payload_end:
