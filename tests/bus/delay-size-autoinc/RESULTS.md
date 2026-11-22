# autoinc results

SCPH-5501, three repetitions, identical every time.

BIOS ROM (DEV2, 8-bit), reading BFC00000h..:

| DEV2 | lw at +0, +4 | lh at +2 | lb +0..+7 |
|---|---|---|---|
| 0013243Fh (BIOS) | 3C080013 3508243F | 3C08 | 13 00 08 3C 3F 24 08 35 |
| 0013043Fh (bit 13 clear) | 13131313 3F3F3F3F | 0808 | 13 00 08 3C 3F 24 08 35 |

With bit 13 clear every byte of a wide read comes from the start address.

SPU voice 0 ADSR (DEV4), shown as 1F801C0Ah:1F801C08h. Seeded with two `sh`
and read back under 200931EFh; only the access under test used the setting.

| DEV4 | sw AAAA5555 over 2222:1111 | sh BEEF over 4444:3333 | lw of 9ABC:5678 |
|---|---|---|---|
| 200931EFh (16-bit) | AAAA:5555 | 4444:BEEF | 9ABC5678 |
| 200911EFh (16-bit, bit 13 clear) | 2222:AAAA | 4444:BEEF | 56785678 |
| 200921EFh (8-bit) | FFAA:FF55 | 4444:FFBE | BCBC7878 |
| 200901EFh (8-bit, bit 13 clear) | 2222:FFAA | 4444:FFBE | 78787878 |

Without bit 13 both halves of a `sw` go to the first register and the second
one wins. In 8-bit mode the SPU still takes a halfword per byte transaction:
the byte lands in bits 0-7, bits 8-15 read as FFh, and A0 is ignored, so the
odd byte overwrites the even one in the same register.
