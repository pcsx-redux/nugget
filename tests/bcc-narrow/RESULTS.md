# bcc-narrow results

SCPH-1000, SCPH-1001, SCPH-5501 and SCPH-7001: every read completes, no
exception, and each value is the same in all three repetitions. BCC read with
lw is 0001E988h. lb at FFFE0130h returns 88h and lh at FFFE0130h returns
E988h, the low byte and halfword. lb at FFFE0131h..33h and lh at FFFE0132h do
not return the BCC bytes: 00h/0000h on the SCPH-1000, 30h, 20h, 02h and 0220h
on the other three.
