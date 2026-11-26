# bcc-store results

SCPH-1000, SCPH-1001, SCPH-5501, SCPH-7001 and SCPH-9002 agree, and every arm is the
same in all three passes. The target is a word in .bss at 800121F0h holding
11223344h before each arm; `ram` is read back through KSEG1, `tag` is the
line's tag read in tag test mode (BCC 0001E90Ch, IsC). Store values end in
5h, so a tag write shows as valid bits 5h.

| BCC | IsC | access | RAM | tag |
|---|---|---|---|---|
| 0001E988h | 0 | sw KSEG0 | written | untouched |
| 0001E98Ch | 0 | sw/sh/sb KSEG0, sw KSEG1 | written (sh/sb: its lanes only) | untouched |
| 0001E90Ch | 0 | sw KSEG0 | written | untouched |
| 0001E908h | 0 | sw KSEG0 | written | untouched |
| 0001E90Ch | 1 | sw KSEG0 | untouched | valid 5h, match |
| 0001E90Ch | 1 | sw KSEG1 | untouched | valid 5h, no match |
| 0001E98Ch | 1 | sw/sh/sb KSEG0 | untouched | valid 5h, match |
| 0001E98Ch | 1 | sw KSEG1 | untouched | valid 5h, no match |
| 0001E988h | 1 | sw KSEG0 | untouched | untouched |

With IsC clear, stores reach RAM whatever TAG and DS are. With IsC set they
never do: TAG set makes it tag test mode with DS set or clear, sub-word stores
included, and without TAG the store does not touch the tag.
