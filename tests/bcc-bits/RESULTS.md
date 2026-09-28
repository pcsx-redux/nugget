# bcc-bits results

SCPH-1000, SCPH-1001 and SCPH-5501 agree. Cycles, minimum of 8 runs; the
noise floor between identical rows is about 2 cycles.

| BCC | ramLoadUse | ramStore | storeLoad | ramLoadNop | spad | mmio | ifetchUnc | dmaContend | dmaDone |
|---|---|---|---|---|---|---|---|---|---|
| 0001E988h (default) | 262 | 69 | 422..424 | 292 | 100 | 163 | 590 | 262 | 4645..4658 |
| INTP set | 262 | 69 | 422..424 | 292 | 100 | 163 | 590..592 | 262 | same |
| RDPRI clear | 261..262 | 69 | 422..424 | 292..293 | 100 | 163 | 590..592 | 262 | same |
| NOPAD clear | 262 | 70 | 422 | 292 | 100 | 163 | 591 | 262 | same |
| BGNT clear | 262 | 69 | 422..424 | 292..293 | 100 | 163 | 590..591 | 262 | never completes |
| LDSCH clear | 263 | 70 | 424 | **357** | 101 | 165 | 591..593 | 263 | same |
| NOSTR set | 262 | 69 | 422..424 | 292 | 100 | 163 | **622..624** | 262 | same |
| IS1 clear (control) | 590 | 316..317 | 722..725 | 1114 | 592 | 296 | 590 | 590..592 | 5114..5120 |

With BGNT clear the DMA6 transfer is still pending when the row ends, and it
completes once BCC is back to the default value, on all three consoles.

INTP is flipped with IRQs masked, so interrupt delivery is not covered.
