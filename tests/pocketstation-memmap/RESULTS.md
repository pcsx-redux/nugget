# pocketstation-memmap results

SCPH-5501, PocketStation in slot 1. All frames well formed (LEN1, LEN2 and
trailer checked on every read and write). Controls: BIOS ROM reads back ARM
opcodes, stable across reads; kernel RAM 0F4h keeps a written pattern.

- F_xxx (06000014h-060000FFh, 06000140h-060002FFh, 06000400h-06FFFFFFh),
  LCD_xxx (0D000008h-0D0000FFh, 0D000180h-0D7FFFFFh) and BATT_xxx
  (0D800024h-0DFFFFFFh): zero at every offset read (up to the top of each
  range, power-of-two steps) while the block's real registers are non-zero
  (F_CTRL 1, F_WAIT 4/4, F_BANK_VAL 0Fh, LCD_MODE D8h, LCD_CAL 12h,
  IOP_CTRL 0Fh, 0D80000Ch 12h). 5AA5C33Ch written to each range reads back
  zero, and no readable register of the block changes.
- COM_xxx: during the transfer that reads it, 0C00001Ch-0C7FFFFFh reads
  00000002h at every offset, as do 0C00000Ch, COM_STAT2 and COM_CTRL2.
  COM_MODE and COM_CTRL1 read 3, COM_STAT1 0. COM_DATA is not read (it would
  pop the link's own RX byte), and the COM range is not written.
- Open: the first read of LCD_MODE/LCD_CAL right after another block's
  write test once returned 6487h in the upper halfword (648700D8h,
  64870012h), then D8h/12h on the next read.
