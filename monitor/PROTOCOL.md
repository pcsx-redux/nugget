# Monitor wire protocol

This is the wire protocol of the resident debug monitor in `monitor/`, as the
code implements it. The monitor owns the break and fault exception paths of
the PS1 it runs on and gives a host memory access, register access, program
loading and launch, hardware breakpoints, PCDRV host file I/O, and exit-code
readback over one frame format.

Sources:

| File | Content |
|------|---------|
| `monitor/monitor.h` | Opcodes, error codes, stop reasons, protocol version |
| `monitor/monitor.c` | Command loop, events, exception hook |
| `monitor/transport.h`, `monitor/transport.c` | Frame layer, checksum, stream framing, SET_BAUD windows |
| `monitor/link.h` | Link selection, word-over-byte packing for stream links |
| `monitor/link-atcons.h` | ATCONS word link |
| `monitor/link-sio1.h` | SIO1 byte stream link |
| `monitor/pcdrv.h`, `monitor/pcdrv.c` | PCDRV servicing |
| `monitor/cop0dbg.h` | cop0 debug registers, ExcCode values |
| `monitor/hosts/` | Hosts that run the monitor on a retail BIOS kernel |
| `openbios/` (`make BOOT=cart MONITOR=1`) | Host that runs the monitor inside OpenBIOS on a DTL-H2700 |

## 1. Hosts

The monitor is one set of sources built into different hosts. Each build
selects exactly one link (`MONITOR_LINK_ATCONS` or `MONITOR_LINK_SIO1`).

| Host | Build | Kernel | Link | Console text |
|------|-------|--------|------|--------------|
| OpenBIOS, DTL-H2700 | `openbios`: `make BOOT=cart MONITOR=1` | OpenBIOS | ATCONS word channel | ATCONS byte channel (OpenBIOS tty) |
| Retail PS-EXE | `monitor/hosts/retail` | Retail BIOS | SIO1 byte stream | Same SIO1 stream, TTY mode |
| Expansion ROM cart | `monitor/hosts/cart` | Retail BIOS | SIO1 byte stream | Same SIO1 stream, TTY mode |

The monitor uses only kernel interfaces that both kernels provide: the
table of tables at `0x100`, the RAM size word at `0x60` (MiB), and the A0/B0/C0
calls.

## 2. Transports

### 2.1 ATCONS word link (`MONITOR_LINK_ATCONS`)

The DTL-H2700 ATCONS block sits behind EXP2 at `0x1F802000`.

| Register | Address | Width | Use |
|----------|---------|-------|-----|
| `ATCONS_STAT` | `0x1F802000` | u8 | Status |
| `ATCONS_FIFO` | `0x1F802002` | u8 | Byte channel (tty) |
| `ATCONS_WORD` | `0x1F802004` | u16 | Word channel (monitor frames) |
| `ATCONS_IRQ` | `0x1F802030` | u8 | IRQ handshake |
| `ATCONS_IRQ2` | `0x1F802032` | u8 | IRQ handshake |

| STAT bit | Meaning |
|----------|---------|
| 0 (`0x01`) | RX word available, host -> PS1 |
| 2 (`0x04`) | TX word ready, PS1 -> host |
| 3 (`0x08`) | TX byte ready, PS1 -> host |
| 4 (`0x10`) | RX byte available, host -> PS1 |

- Framing: every frame travels on the word channel as 16-bit words. There is
  no TTY mode and no leading `0x00` on this link. The receiver discards words
  until it reads SYNC.
- Word I/O is polled on STAT alone: spin on bit 0 then `lhu` for a read, spin
  on bit 2 then `sh` for a write. The word channel takes no per-word IRQ
  acknowledge.
- Flow control: both directions block on the STAT bits. Words the host posts
  while the monitor is not receiving stay in the channel until the next
  receive reads them.
- Link init mirrors the OpenBIOS tty init: clear bit 0 of `ATCONS_IRQ2`, write
  `0x20` to `ATCONS_IRQ`, set bit 4 of `ATCONS_IRQ2`.
- Console text: OpenBIOS (built with `OPENBIOS_INSTALL_TTY_CONSOLE`) runs its
  tty device on the byte channel, both directions (stdout on bit 3, stdin on
  bit 4). The monitor never reads or writes the byte channel. The monitor's
  start-up banner `OpenBIOS Monitor.` goes out on the byte channel.
- Max LEN: 65535 (the u16 field). The command loop still limits small
  commands to 16 payload words (section 5).
- Checksum: a received CKSUM of `0x00000000` is accepted without verifying.
- No line rate: SET_BAUD answers `ERROR(EBADCMD)`.
- The bus must address EXP2 as a 16-bit device: the monitor build of
  OpenBIOS writes `DEV8_CTRL` (`0x1F80101C`) as `0x81022` instead of the
  retail `0x80777`. With the 8-bit setting each word access splits into two
  byte cycles and the word channel does not respond.

### 2.2 SIO1 byte stream (`MONITOR_LINK_SIO1`)

`link-sio1.h` defines `MONITOR_LINK_IS_STREAM` and `MONITOR_LINK_HAS_RATE`.

| Setting | Value |
|---------|-------|
| Mode | `0x4E`: 8 data bits, no parity, 1 stop bit, x16 |
| Reload at init | `MONITOR_SIO1_RELOAD`, default 18 (build-time) |
| Nominal rate | `2073600 / reload` (18 -> 115200, 9 -> 230400, 5 -> 414720, 4 -> 518400) |
| Rate the PS1 produces | `33868800 / (16 * reload)` (18 -> 117600) |
| RX FIFO | 8 bytes |

- Framing: byte stream, section 2.3. Every frame word is two bytes, low byte
  first.
- Flow control, PS1 transmit: the SIO1 hardware gates TX on CTS, which is the
  host's RTS. Console text and frames from the PS1 stop while the host holds
  RTS low.
- Flow control, PS1 receive: the monitor raises its RTS only while it is
  inside a receive (from the start of a frame hunt to the end of the frame's
  checksum) and lowers it after. The hardware never drops RTS on its own. The
  8-byte FIFO absorbs a few bytes sent after RTS drops. The receive overrun
  bit is not checked.
- A host that cannot observe the PS1's RTS must send only when the monitor
  is waiting for a frame: in HALTED after the response to the previous
  command, and after a PCDRV_REQ. Section 5 lists when that holds.
- Console text, PS1 -> host: `monitor/hosts/sio1-tty.c` replaces the kernel
  `tty` device and reopens stdin/stdout on it. Its writes go out on SIO1 as
  TTY-mode bytes; `0x00` bytes are dropped. Reads return nothing. Since the
  monitor is single-threaded, console bytes never land inside a frame.
- Console text, host -> PS1: non-zero bytes the monitor sees while hunting
  for a frame are counted and discarded. There is no stdin on this link.
- The retail and cart hosts stub the monitor's own `printf`, so no banner is
  printed.
- Max LEN: `TRANSPORT_STREAM_MAX_LEN` = 4112 words (an 8 KiB payload plus 16
  header words).
- Checksum: mandatory. A received CKSUM of `0x00000000` fails as a checksum
  error.

### 2.3 Stream framing (TTY mode and frame mode)

Applies to links that define `MONITOR_LINK_IS_STREAM` (SIO1). Console text and
frames share one byte stream in each direction.

- TTY mode is the default. Every non-zero byte is console text.
- A `0x00` byte starts a frame. The sender emits exactly one `0x00`, then the
  frame words of section 3, SYNC first, each word low byte first.
- A run of `0x00` bytes is one frame start. The receiver skips the run and
  checks the two bytes after it against SYNC (`0xAA 0x55`). If they differ,
  the run was noise and the receiver returns to TTY mode.
- After SYNC the receiver reads TYPE and LEN. A LEN above 4112 is treated as
  noise: the receiver returns to TTY mode without sending anything.
- Otherwise the receiver consumes exactly LEN payload words and the two
  CKSUM words, then returns to TTY mode. Zero bytes inside a frame are data.
- The PS1 tty driver never emits `0x00`.

Wire bytes of a PING (no payload):

    00  AA 55  01 00  00 00  01 00 02 00
    |   SYNC   TYPE   LEN    CKSUM (0x00020001, low word first)
    frame start

Stream framing is implemented for SIO1 only. The ATCONS link carries frames on
the word channel and console text on the byte channel (section 2.1).

### 2.4 Line rate: SET_BAUD

SET_BAUD `[reload:u16]` changes the SIO1 reload. The sequence, as the monitor
runs it:

1. `reload == 0` -> `ERROR(EBADLEN)`. A link without a rate (ATCONS) ->
   `ERROR(EBADCMD)`. These checks run in that order.
2. The monitor sends ACK at the current rate, waits for the transmitter to
   drain (`TXEMPTY`), and writes the new reload.
3. Window 1: the monitor accepts only the exact bytes of a PING with no
   payload:
   `00 AA 55 01 00 00 00 01 00 02 00`.
   Any other byte is discarded. On a match it sends `PONG [proto_ver]` at the
   new rate.
4. Window 2: the monitor accepts only the exact bytes of the confirmation
   PING, which carries the word 1:
   `00 AA 55 01 00 01 00 01 00 03 00 06 00`.
   On a match it sends `PONG [proto_ver]`, keeps the new rate, and returns to
   the command loop.
5. If either window expires, the monitor writes the previous reload back and
   returns to the command loop. It sends nothing on revert.

A window is `MONITOR_RATE_WINDOW_SPINS` polls of the RX status (build-time,
default 3000000, about 1.08 s on a retail CPU). RTS is raised during each
window.

Host side:

1. Send SET_BAUD, wait for ACK at the old rate.
2. Switch the host UART to the new rate.
3. PING (no payload), repeating as needed, until PONG.
4. Only after that PONG, send the confirmation PING `[1]` and wait for PONG.
5. On failure at any step, return to the old rate, wait longer than two
   windows, then PING at the old rate.

The confirmation differs from the plain PING so that a retried plain PING
cannot confirm a rate at which the host cannot read the monitor's PONG. Step
4 proves PS1 -> host at the new rate; step 3 proves host -> PS1.

Outside the SET_BAUD windows, PING with a payload is an ordinary PING: the
payload is ignored.

## 3. Frame format

    [SYNC:u16] [TYPE:u16] [LEN:u16] [payload: LEN x u16] [CKSUM:u32]

| Field | Value |
|-------|-------|
| SYNC | `0x55AA`. Not covered by the checksum. |
| TYPE | Opcode, section 4 |
| LEN | Payload length in 16-bit words |
| payload | LEN words |
| CKSUM | Fletcher-32 over TYPE, LEN and the payload words, sent as two words, low word first |

Encoding:

- Every multi-byte field is little-endian. A u32 is two words, low half
  first. s32 is the same encoding.
- Byte arrays are packed two bytes per word, low byte first, and padded with
  a zero byte to a whole word. The byte count travels in a separate field.
- A cstr is the NUL-terminated string including its NUL, packed as a byte
  array and padded to a whole word.

Checksum:

    s1 = s2 = 0                      (uint32_t, wrapping)
    for each word w in TYPE, LEN, payload:
        s1 += w; s2 += s1
    ck = ((s2 % 65535) << 16) | (s1 % 65535)
    if ck == 0: ck = 0xFFFFFFFF

The accumulators are 32-bit and wrap; the modulo is applied once at the end.
An implementation with wider integers must mask to 32 bits after each add.
On the wire `0x00000000` means "not computed". The monitor always computes
the checksum. What a received `0` means depends on the link: accepted without
verifying on ATCONS, rejected on SIO1.

Frame size limits:

| Frame | Limit |
|-------|-------|
| Any frame on SIO1 | LEN <= 4112 |
| Any frame on ATCONS | LEN <= 65535 |
| Host commands other than WRITE_MEM and LOAD | LEN <= 16, else `ERROR(EBADLEN)` |
| Bulk data per frame, as the monitor sends it (DATA, PCDRV_REQ for PCwrite) | 8192 bytes |

## 4. Opcodes

The high nibble of TYPE gives the direction: `0x0_` and `0x2_` host -> PS1,
`0x4_` PS1 response, `0x8_` PS1 event.

Host -> PS1:

| TYPE | Name | Handled in |
|------|------|------------|
| `0x01` | PING | HALTED; also SET_BAUD windows |
| `0x02` | READ_MEM | HALTED |
| `0x03` | WRITE_MEM | HALTED |
| `0x04` | GET_REGS | HALTED |
| `0x05` | SET_REG | HALTED |
| `0x06` | SET_BP | HALTED |
| `0x07` | CLR_BP | HALTED |
| `0x08` | LOAD | HALTED |
| `0x09` | RUN | HALTED -> RUNNING |
| `0x0A` | CONT | HALTED -> RUNNING |
| `0x0B` | STOP | HALTED (no-op, answers ACK) |
| `0x0C` | STEP | Reserved; answers `ERROR(EBADCMD)` |
| `0x0D` | SET_BAUD | HALTED |
| `0x20` | PCDRV_RESP | Only as the answer to a PCDRV_REQ |

PS1 -> host:

| TYPE | Name | Payload |
|------|------|---------|
| `0x40` | ACK | none |
| `0x41` | DATA | READ_MEM data |
| `0x42` | REGS | GET_REGS data |
| `0x43` | PONG | `[proto_ver:u16]` |
| `0x4F` | ERROR | `[errcode:u16]` |
| `0x80` | HELLO | Monitor entry announce |
| `0x81` | STOPPED | Stop event |
| `0x82` | PCDRV_REQ | Host file request |

`proto_ver` is `MON_PROTO_VER` = `0x0001`.

## 5. States

| State | Monitor activity | Link direction |
|-------|------------------|----------------|
| HALTED | Command loop, inside exception context | Host sends one command, monitor answers, repeat |
| RUNNING | Target executes; the monitor is not reading the link | PS1 -> host events only; host -> PS1 only PCDRV_RESP after a PCDRV_REQ |

Transitions:

| From | Trigger | To |
|------|---------|----|
| (entry) | `break 4, 1` at monitor start: HELLO | HALTED |
| HALTED | RUN, CONT (after ACK) | RUNNING |
| RUNNING | STOPPED event (breakpoint, watch, fault, exit) | HALTED |
| RUNNING | PCDRV_REQ ... final PCDRV_RESP | RUNNING |

- In HALTED the monitor emits no unsolicited frames.
- In RUNNING the host sends nothing except PCDRV_RESP. On SIO1 the
  monitor's RTS is low. On ATCONS anything the host posts waits in the word
  channel and is read by the next receive: as the PCDRV_RESP of the next
  PCDRV_REQ, or as the first command after the next stop.
- There is no way to stop a running target from the host (section 12).

Command loop, per frame:

1. Hunt for a frame (section 2.1 or 2.3), read TYPE and LEN.
2. WRITE_MEM and LOAD: decode the payload straight into target memory, then
   verify the checksum (section 6).
3. Anything else: buffer up to 16 payload words, drain the rest, verify the
   checksum.
4. Checksum failure -> `ERROR(ECKSUM)`. Otherwise LEN > 16 ->
   `ERROR(EBADLEN)`. Otherwise dispatch. Unknown TYPE -> `ERROR(EBADCMD)`.

Payload length is not checked against the opcode. A command frame shorter
than its layout is dispatched with stale words from the previous command in
the missing fields.

## 6. Commands

Each entry: request payload -> response. Offsets are in words.

| Command | Request payload | Response |
|---------|-----------------|----------|
| PING | none (payload ignored) | `PONG [proto_ver:u16]` |
| READ_MEM | `[addr:u32][len:u32]` | One or more DATA frames |
| WRITE_MEM | `[addr:u32][len:u32][bytes]` | ACK, or `ERROR(ECKSUM)` |
| LOAD | `[addr:u32][len:u32][bytes]` | ACK, or `ERROR(ECKSUM)` |
| GET_REGS | none | `REGS [38 x u32]` |
| SET_REG | `[idx:u16][value:u32]` | ACK, `ERROR(EBADREG)`, `ERROR(EBADSTATE)` |
| SET_BP | `[kind:u16][addr:u32][mask:u32]` | ACK, `ERROR(EBADCMD)` |
| CLR_BP | `[kind:u16]` | ACK |
| RUN | `[pc:u32][gp:u32][sp:u32]` | ACK, then RUNNING |
| CONT | none | ACK then RUNNING, or `ERROR(EBADSTATE)` |
| STOP | none | ACK |
| SET_BAUD | `[reload:u16]` | Section 2.4 |

READ_MEM:

- The monitor sends DATA frames of `[nbytes:u32][bytes]`, where `nbytes` is
  the byte count in that frame, at most 8192. LEN = 2 + ceil(nbytes / 2).
  The host concatenates frames until it has `len` bytes.
- `len == 0` produces one DATA frame with `nbytes == 0`.
- Memory is read one byte at a time (`lbu`).
- `addr` is not validated.

WRITE_MEM and LOAD:

- The two are identical on the wire and in effect. LOAD keeps no extent
  record.
- `len` is the byte count. The payload after the 4 header words carries
  `ceil(len / 2)` words; bytes past `len` in the payload are discarded, and
  if the payload is shorter than `len` only the bytes present are written.
- Bytes are stored one at a time (`sb`) as they arrive, before the checksum
  is known. On `ERROR(ECKSUM)` the bytes are already in memory.
- One frame may carry any length up to the link's LEN limit. The host sends
  8 KiB per frame (LEN 4100); that is required on SIO1 and conventional on
  ATCONS.
- The payload must hold at least the 4 header words.
- `addr` is not validated. The instruction cache is not flushed.

GET_REGS: section 9. With no halted context (before the first stop, or after
RUN) every slot is 0 except BadVaddr.

SET_REG:

- `idx` indexes the REGS table (0..37). `idx > 37` -> `ERROR(EBADREG)`.
- With no halted context -> `ERROR(EBADSTATE)`.
- `idx == 0` is accepted and ignored. `idx == 35` sets the monitor's copy of
  BadVaddr only. `idx == 37` sets the resume PC.

SET_BP, CLR_BP: section 11.

RUN:

- Writes the current thread's register frame: all GPRs 0, then `gp`, `sp`,
  and `fp = sp`; resume PC = `pc`; HI = LO = 0; Cause = 0;
  SR = `0x40000404` (CU2, IM2, IEp).
- Clears the halted context, sends ACK, and returns from the exception into
  `pc`.
- Accepted in HALTED whether or not a program was stopped.
- The instruction cache is not flushed.

CONT: resumes the halted context, including any SET_REG changes. With no
halted context -> `ERROR(EBADSTATE)`. The resume PC for each stop reason is
in section 11.

STOP: in HALTED it answers ACK and does nothing else. While RUNNING it is
not read (section 5).

## 7. Events

HELLO, LEN 5:

| Word | Field | Value |
|------|-------|-------|
| 0 | `proto_ver:u16` | `0x0001` |
| 1-2 | `sram_base:u32` | `0x1FA00000` (constant, every host) |
| 3-4 | `ram_size:u32` | Kernel RAM size word at `0x60`, shifted left 20 |

HELLO is sent each time the monitor is entered through `break 4, 1`, which in
the shipped hosts is once, at start-up. On ATCONS a HELLO nobody has read
stays in the word channel: a host attaching later reads the pending HELLO
before its first PING, or it takes the HELLO for the PING's reply. On SIO1 a
HELLO sent before the host listens is lost; the host attaches with PING.

STOPPED, LEN 7:

| Word | Field |
|------|-------|
| 0 | `reason:u16` |
| 1-2 | `epc:u32`, the resume PC of the halted context |
| 3-4 | `a:u32` |
| 5-6 | `b:u32` |

| reason | Name | a | b | epc |
|--------|------|---|---|-----|
| `0x01` | BREAKPOINT | 0 | 0 | Hardware exec breakpoint: the instruction. Software `break`: the instruction after it |
| `0x02` | INTERRUPT | - | - | Never sent (section 12) |
| `0x03` | DATA_WATCH | Armed watch address (BDA) | 0 | The instruction |
| `0x04` | FAULT | ExcCode | BadVaddr for ExcCode 4, 5; else 0 | The faulting instruction |
| `0x05` | EXIT | Exit code (`$a0`) | 0 | The `break 4, 0` itself |

Every STOPPED enters HALTED.

PCDRV_REQ: section 8.

## 8. PCDRV

A target's PCDRV call (`break 0, 0x101` .. `break 0, 0x107`, as issued by
`common/kernel/pcdrv.h`) traps into the monitor, which exchanges PCDRV_REQ
and PCDRV_RESP frames with the host, writes the result into the trapped
registers, steps the resume PC past the `break`, and returns to the target.
The target stays RUNNING. The whole exchange, including every continuation
frame, is one transaction: no other frame is sent or accepted in between.

The monitor reads and writes all target memory itself (the file name, the
read and write buffers). The host never reaches into PS1 memory.

Every PCDRV frame starts with `op:u32`, the break code2 value. PCDRV_RESP
echoes the op of the request.

| op | Call | Arguments | PCDRV_REQ payload | PCDRV_RESP payload | Result |
|----|------|-----------|-------------------|--------------------|--------|
| `0x101` | PCinit | - | `[op]` | `[op][ret:s32]` | v0 = ret |
| `0x102` | PCcreat | a0 = name, a2 = mode | `[op][mode:u32][name:cstr]` | `[op][ret:s32]` | v0 = 0, v1 = ret |
| `0x103` | PCopen | a0 = name, a2 = flags | `[op][flags:u32][name:cstr]` | `[op][ret:s32]` | v0 = 0, v1 = ret |
| `0x104` | PCclose | a0 = fd | `[op][fd:u32]` | `[op][ret:s32]` | v0 = ret |
| `0x105` | PCread | a1 = fd, a2 = len, a3 = buf | `[op][fd:u32][len:u32]` | `[op][ret:s32][bytes]`, continuations | v0 = 0, v1 = ret |
| `0x106` | PCwrite | a1 = fd, a2 = len, a3 = buf | `[op][fd:u32][len:u32][bytes]`, continuations | `[op][ret:s32]` | v0 = 0, v1 = ret |
| `0x107` | PClseek | a0 = fd, a2 = offset, a3 = whence | `[op][fd:u32][offset:s32][whence:u32]` | `[op][ret:s32]` | v0 = 0, v1 = ret |

The `pcdrv.h` wrappers return v1 when v0 is 0, else -1. `ret` is the host's
result: a file descriptor, a byte count, a new position, or a negative value
for failure. The monitor passes it through unchanged.

Scalar responses (every op but PCread):

- The monitor reads one PCDRV_RESP frame. Words past `[op][ret]` are
  ignored.
- ret becomes -1 if the frame fails its checksum, TYPE is not PCDRV_RESP,
  the op does not match, or LEN < 4. No ERROR frame is sent in any of these
  cases; the frame is consumed.

PCwrite:

- A negative `len` (a2 as s32) fails with v1 = -1 and no frame is sent.
- The first frame is `PCDRV_REQ [op][fd][len][bytes]` with at most 8192
  bytes. While bytes remain, the monitor sends `PCDRV_REQ [op][bytes]`
  continuation frames of at most 8192 bytes each. `len == 0` sends one frame
  with no bytes.
- One scalar PCDRV_RESP answers the whole write.

PCread:

- The host answers `PCDRV_RESP [op][ret][bytes]`. If `ret > 0` the host
  sends `ret` bytes in total: as many as fit in the first frame, then
  `PCDRV_RESP [op][bytes]` continuation frames until `ret` bytes have been
  sent. The host sends 8192 bytes per frame; on SIO1 any frame must fit
  LEN <= 4112.
- The monitor places each frame's bytes at the running byte offset and adds
  two bytes per payload word to that offset, so every frame but the last
  must carry an even number of bytes.
- The monitor stores at most `len` bytes, the length the target asked for.
  If `ret > len` the excess is read and discarded and the call returns -1.
- The call returns -1 if any frame fails its checksum, has the wrong TYPE or
  op, or if a continuation frame carries no bytes (LEN <= 2). Bytes that
  arrived before the failure may already be in the target's buffer.
- `ret <= 0` ends the transaction after the first frame; ret is returned
  as is.

A receiver that gets the whole transfer in its first frame never looks for
a continuation, so a single large frame works on ATCONS, where LEN is not
capped at 4112.

## 9. REGS layout

REGS is 38 u32 values (LEN 76), in gdb g-packet order. SET_REG `idx` uses the
same indices.

| Index | Register | Source |
|-------|----------|--------|
| 0-31 | r0-r31 | Saved GPRs (r0 is 0) |
| 32 | SR | Saved cop0r12 |
| 33 | LO | Saved LO |
| 34 | HI | Saved HI |
| 35 | BadVaddr | cop0r8 captured at the last ADEL/ADES fault; 0 after any other stop |
| 36 | Cause | Saved cop0r13 |
| 37 | PC | Resume PC of the halted context (see section 7 for its value per reason) |

The R3000A has no FPU; a gdb bridge fills gdb's FP slots with 0.

## 10. Error codes

`ERROR [errcode:u16]`:

| Code | Name | Sent when |
|------|------|-----------|
| `0x01` | EBADCMD | Unknown TYPE (including STEP, and PCDRV_RESP outside a PCDRV transaction); SET_BP kind > 3; SET_BAUD on a link without a rate |
| `0x02` | EBADSTATE | SET_REG or CONT with no halted context |
| `0x03` | EBADADDR | Defined, never sent |
| `0x04` | EBADREG | SET_REG idx > 37 |
| `0x05` | EBADLEN | Command payload over 16 words (not WRITE_MEM/LOAD); SET_BAUD reload 0 |
| `0x06` | ECKSUM | Checksum mismatch on a command frame (on SIO1, also a CKSUM of 0) |
| `0x07` | ENOFD | Defined, never sent |

After an ERROR the monitor is back in the command loop. There is no NAK and
no retransmission: the host decides whether to resend.

## 11. Breakpoints, stops and stepping

Breakpoints use the cop0 debug unit: one exec breakpoint (BPC cop0r3, mask
BPCM cop0r11) and one data breakpoint (BDA cop0r5, mask BDAM cop0r9), both
controlled by DCIC (cop0r7). No instruction is patched, so breakpoints work
on read-only memory.

SET_BP `kind`:

| kind | Type | Registers | DCIC bits set |
|------|------|-----------|---------------|
| 0 | Exec | BPC = addr, BPCM = mask | DE, PCE, TR, KD, UD |
| 1 | Data read | BDA = addr, BDAM = mask | DE, DAE, TR, KD, UD, DR |
| 2 | Data write | BDA = addr, BDAM = mask | DE, DAE, TR, KD, UD, DW |
| 3 | Data read/write | BDA = addr, BDAM = mask | DE, DAE, TR, KD, UD, DR, DW |

| DCIC bit | Name |
|----------|------|
| 23 | DE, master enable |
| 24 | PCE, exec breakpoint enable |
| 25 | DAE, data breakpoint enable |
| 26 | DR, break on read |
| 27 | DW, break on write |
| 29 | KD, kernel mode |
| 30 | UD, user mode |
| 31 | TR, trap |

- `mask = 0xFFFFFFFF` matches the exact address; clearing mask bits widens
  the match.
- SET_BP ORs its bits into the running DCIC image. Setting a second data
  kind without CLR_BP keeps the first kind's DR/DW bits; clear first to
  change kind.
- CLR_BP kind 0 clears PCE. Any other kind clears DAE, DR and DW. When
  neither PCE nor DAE remains, DCIC is written as 0.
- A hardware breakpoint hit writes DCIC to 0: both breakpoints are disarmed
  and must be set again.
- The monitor copies the general exception trampoline at `0x80` to the
  cop0 break vector at `0x40`, so both vectors reach the same handler.

Exception dispatch (the monitor's handler runs at priority 0, ahead of the
kernel's syscall handler):

| ExcCode | Condition | Action |
|---------|-----------|--------|
| 9 (BP) | Word at resume PC has function field `0x0D` (`break`) | Software break, decoded below |
| 9 (BP) | Otherwise | Hardware breakpoint: DATA_WATCH if DCIC had DAE set, else BREAKPOINT |
| 4, 5 (AdEL, AdES) | - | FAULT, a = ExcCode, b = BadVaddr |
| 6, 7, 10, 11, 12 (IBE, DBE, RI, CpU, Ov) | - | FAULT, a = ExcCode, b = 0 |
| 0, 8, others | - | Not handled; passed to the kernel |

Software `break code1, code2` (code1 = bits 25:16, code2 = bits 15:6):

| code1 | code2 | Action |
|-------|-------|--------|
| 0 | `0x101`-`0x107` | PCDRV (section 8), resume after the break |
| 4 | 0 | STOPPED EXIT, a = `$a0`, resume PC left on the break |
| 4 | 1 | Monitor entry: HELLO, then the command loop; halted context unchanged |
| any other | any other | STOPPED BREAKPOINT, resume PC advanced past the break |

Resume behaviour with CONT:

- Software break: continues after the break.
- Hardware breakpoint or watch: re-executes the instruction; the breakpoint
  is already disarmed, so it does not trap again.
- FAULT: re-executes the faulting instruction unless the host changes PC
  (SET_REG 37).
- EXIT: re-executes `break 4, 0` and stops again with EXIT.

STEP (`0x0C`) is reserved and answers `ERROR(EBADCMD)`. Single-stepping can
be built on the host from GET_REGS, READ_MEM and a one-shot exec breakpoint
at the computed next PC; no protocol change is needed for that.

## 12. Bootstrap

### 12.1 OpenBIOS on the DTL-H2700 (ATCONS)

- Build: `make BOOT=cart MONITOR=1` in `openbios/`. This defines
  `OPENBIOS_H2X00_MONITOR` and `MONITOR_LINK_ATCONS`, strips the shell and
  CD-ROM boot, installs the tty console on ATCONS, shows an inverted splash
  screen, and links `ROM_BASE = 0xBFC40000`, `ROM_SIZE = 0x20000`,
  entry `_h2700_hook`.
- Flash image: `openbios/h2700/flash/mkimage.py <stock> <elf> <out>` takes a
  512 KiB H2700 flash image, checks for the stock entry jump
  (`j 0xBFC07180`) at `0xBFC00414`, places the monitor at flash offset
  `0x40000` (`0xBFC40000`), and replaces the entry jump with `j` to the
  monitor's entry point. `openbios/h2700/flash` is a flash programmer that
  runs as a target under a RAM-resident monitor.
- Boot select: `_h2700_hook` reads the reset-mode byte at `0x1F802040`
  (`DTLH_DIPSW`). Value 7 jumps to OpenBIOS `_reset`; any other value jumps
  to the stock BIOS at `0xBFC07180`. The host selects the monitor by the
  reset mode it sets.
- OpenBIOS cold-initialises as the kernel, then `main()` calls
  `monitorMain()` in place of the shell and game boot.
- RAM build: `MONITOR_ROM_BASE=0x80020000 MONITOR_ENTRYPOINT=_reset` links
  the same monitor into main RAM, to be loaded and started by any loader.
  Targets then load at `0x80010000`, below it.

### 12.2 Retail PS-EXE (SIO1)

- Build: `monitor/hosts/retail`, a PS-EXE linked at `0x801C0000` so targets
  keep the usual `0x80010000`. `MONITOR_SIO1_RELOAD` sets the initial rate.
- Start by any means that runs a PS-EXE on a retail BIOS:
  - `make iso` builds a disc image with the EXE as `PSX.EXE` and no
    `SYSTEM.CNF` (needs `exe2iso`; the disc has no license data, so the
    console must boot unlicensed discs).
  - A SIO1 loader already on the console (for example Unirom) uploads and
    runs the EXE. The monitor then takes over SIO1.
- `main()` installs the SIO1 tty (section 2.2) and calls `monitorMain()`.

### 12.3 Expansion ROM cart (SIO1)

- Build: `monitor/hosts/cart` builds the same PS-EXE and wraps it with
  `rom.s` into an image linked at `0x1F000000` (EXP1).
- The image holds the license strings at `0x04` and `0x84` and the
  pre-boot entry pointer at `0x80`. The BIOS calls the pre-boot entry before
  the kernel is set up. The pre-boot code copies a jump to `start` into the
  `0x40` debug vector, arms a cop0 data-write breakpoint on `0x80030000`
  (DCIC `0xEB800000`), and returns.
- When the BIOS writes the shell to `0x80030000` with the kernel up, the
  breakpoint fires; `start` disarms it, copies the appended PS-EXE to its
  load address, and jumps to its entry, still inside the exception.
- `main()` then masks and acknowledges all IRQs, restores the default
  exception return, leaves the critical section, installs the SIO1 tty, and
  calls `monitorMain()`.
- `monitor/tools/cartflash` programs an Am29F010 cart flash with a payload,
  run as a target under the monitor.

### 12.4 Monitor start (every host)

`monitorMain()`:

1. Prints the banner (ATCONS tty only).
2. Initialises the link.
3. Clears the halted context, BadVaddr, DCIC image and watch address.
4. Installs its exception handler at priority 0 and copies the `0x80`
   trampoline to `0x40`.
5. Executes `break 4, 1`: the handler sends HELLO and enters the command
   loop in exception context.

### 12.5 Host session

1. Attach: on ATCONS read a pending HELLO if one is there; on SIO1 listen at
   the build's initial rate. PING until PONG.
2. On SIO1, optionally SET_BAUD (section 2.4).
3. LOAD the program in 8 KiB frames, then RUN with its entry PC, gp and sp.
4. While RUNNING: collect console text, answer PCDRV_REQ, wait for STOPPED.
5. After STOPPED: inspect or modify with READ_MEM, WRITE_MEM, GET_REGS,
   SET_REG, SET_BP, CLR_BP; then CONT, or LOAD and RUN the next program.

## 13. Not implemented

- STOP while RUNNING. The monitor does not read the link while a target
  runs, so a running target cannot be interrupted from the host. STOPPED
  reason INTERRUPT (`0x02`) is never sent. A target that never stops needs a
  reset.
- STEP (`0x0C`).
- NAK and retransmission. A checksum failure is reported with
  `ERROR(ECKSUM)` on commands and as -1 to the target in PCDRV; nothing is
  resent by the monitor.
- Address validation: EBADADDR is never sent; READ_MEM, WRITE_MEM and LOAD
  access whatever address they are given.
- ENOFD is never sent; PCDRV failures travel in `ret`.
- LOAD extent bookkeeping: LOAD behaves as WRITE_MEM.
- Instruction cache flush after WRITE_MEM, LOAD, or before RUN.
- Console input on SIO1: host -> PS1 console bytes are discarded.
- Stream framing on ATCONS: the ATCONS link uses the word channel only.
- SIO1 receive overrun detection.
- Cause.BD handling. For an exception in a branch delay slot the resume PC
  is the branch, and the monitor decodes the word there, so a `break` in a
  delay slot is not recognised as a software break.
- Instruction check: any word whose low 6 bits are `0x0D` counts as a
  `break`; the primary opcode is not checked.
