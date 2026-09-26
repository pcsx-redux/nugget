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

/* Monitor frame layer, over whichever 16-bit word link the build selects
   (monitor/link.h). Frame shape (protocol design section 2):
     [SYNC:u16=0x55AA] [TYPE:u16] [LEN:u16] [payload: LEN words] [CKSUM:u32]
   CKSUM is Fletcher-32 over the 16-bit words TYPE, LEN, payload (sent
   endian (each word contributes low byte then high byte), transmitted
   low-word-first. SYNC is not part of the checksum. */

#define FRAME_SYNC 0x55AA

/* Receive return codes. 0 = a valid frame was decoded. */
#define TRANSPORT_OK 0
#define TRANSPORT_EBADLEN (-1) /* payload longer than caller's buffer */
#define TRANSPORT_ECKSUM (-2)  /* checksum mismatch */

/* Largest LEN a stream link accepts: an 8 KiB bulk payload plus the header
   words any frame type puts in front of it (DESIGN section 13). */
#define TRANSPORT_STREAM_MAX_LEN (4096 + 16)

/* Bring up the link. Idempotent; safe to call once at monitor entry. */
void transportInit(void);

/* Send one frame on the word channel. Blocks on the STAT TX-word-ready bit
   (bit2) for each word. */
void transportSendFrame(uint16_t type, const uint16_t *payload, uint16_t len);

/* Streaming send, for responses whose payload is generated on the fly (e.g.
   READ_MEM copying straight out of target memory without a staging buffer).
   Call Begin with the exact word count, push exactly that many words with
   SendWord, then End. Not reentrant: one frame at a time (the monitor is
   single-threaded). */
void transportSendBegin(uint16_t type, uint16_t len);
void transportSendWord(uint16_t w);
void transportSendEnd(void);

/* Block until a full, checksum-valid frame arrives. On success returns
   TRANSPORT_OK and fills type/payload/lenOut; payload holds up to maxLen
   words. On a length overflow or checksum mismatch the frame is fully drained
   (both ends stay word-aligned) and a negative TRANSPORT_* code is returned. */
int transportRecvFrame(uint16_t *type, uint16_t *payload, uint16_t maxLen, uint16_t *lenOut);

/* Streaming receive, the counterpart to the streaming send. Lets a caller pull
   a frame's payload word by word straight into its final destination (e.g.
   PCread writing decoded bytes directly to the target's own buffer, no staging
   RAM, no memcpy). Call Begin (returns TYPE and payload word count), pull
   exactly `len` words with RecvWord, then End (validates the checksum). Not
   reentrant: one frame at a time. */
void transportRecvBegin(uint16_t *type, uint16_t *len);
uint16_t transportRecvWord(void);
int transportRecvEnd(void);
