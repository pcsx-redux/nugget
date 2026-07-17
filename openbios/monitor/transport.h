/*

MIT License

Copyright (c) 2026 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <stdint.h>

/* H2x00 monitor transport: the ATCONS 16-bit word channel (0x1F802004).
   Frame shape (protocol design section 2):
     [SYNC:u16=0x55AA] [TYPE:u16] [LEN:u16] [payload: LEN words] [CKSUM:u32]
   CKSUM is djb2 (seed 5381) over the TYPE+LEN+payload bytes, native little
   endian (each word contributes low byte then high byte), transmitted
   low-word-first. SYNC is not part of the checksum. */

#define FRAME_SYNC 0x55AA
#define DJB2_SEED 5381u

/* Receive return codes. 0 = a valid frame was decoded. */
#define TRANSPORT_OK 0
#define TRANSPORT_EBADLEN (-1) /* payload longer than caller's buffer */
#define TRANSPORT_ECKSUM (-2)  /* checksum mismatch */

/* Bring up the ATCONS IRQ machinery (mirrors the DTL-H2000 dev_tty_init
   sequence). Idempotent; safe to call once at monitor entry. */
void transportInit(void);

/* Send one frame on the word channel. Blocks on the STAT TX-word-ready bit
   (bit2) for each word. */
void transportSendFrame(uint16_t type, const uint16_t *payload, uint16_t len);

/* Block until a full, checksum-valid frame arrives. On success returns
   TRANSPORT_OK and fills type/payload/lenOut; payload holds up to maxLen
   words. On a length overflow or checksum mismatch the frame is fully drained
   (both ends stay word-aligned) and a negative TRANSPORT_* code is returned. */
int transportRecvFrame(uint16_t *type, uint16_t *payload, uint16_t maxLen, uint16_t *lenOut);
