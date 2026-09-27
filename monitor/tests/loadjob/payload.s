    .section .rodata
    .align 2
    .global g_payload
    .global g_payloadEnd
g_payload:
    .incbin "payload.bin"
g_payloadEnd:
