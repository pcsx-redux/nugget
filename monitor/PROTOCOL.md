# Monitor wire protocol

This is the wire protocol of the resident debug monitor in `monitor/`, as the
code implements it. The monitor owns the break and fault exception paths of
the PS1 it runs on and gives a host memory access, register access, program
loading (plain or LZ4-compressed) and launch, hardware breakpoints, and on
the byte links a host-requested stop of a running target, over one frame
format. Every software `break` stops the target and is reported to
the host, which serves PCDRV host file I/O and reads exit codes from those
stops.

Sources:

| File | Content |
|------|---------|
| `monitor/monitor.h` | Opcodes, flags, capability bits, error codes, stop reasons, protocol version |
| `monitor/monitor.c` | Command loop, events, exception hook |
| `monitor/install.h` | Kernel hook, cop0 break vector, monitor entry, BIOS checksum |
| `monitor/transport.h`, `monitor/transport.c` | Frame layer, checksum, stream framing, SET_BAUD windows |
| `monitor/link.h` | Link selection, word-over-byte packing for stream links |
| `monitor/link-atcons.h` | ATCONS word link |
| `monitor/link-sio1.h` | SIO1 byte stream link |
| `monitor/link-ft232h.h` | FT232H byte stream link |
| `monitor/lz4stream.h`, `monitor/lz4stream.c` | Byte-at-a-time LZ4 block decoder |
| `monitor/cop0dbg.h` | cop0 debug registers, ExcCode values |
| `monitor/hosts/` | Hosts that run the monitor on a retail BIOS kernel |
| `monitor/hosts/retail/core/` | Resident core of the retail and cart hosts, `core.ld` |
| `monitor/hosts/ft232h-boards.mk` | FT232H register addresses per board |
| `openbios/` (`make MONITOR=1`) | Host that runs the monitor inside OpenBIOS |

## 1. Hosts

The monitor is one set of sources built into different hosts. Each build
selects exactly one link (`MONITOR_LINK_ATCONS`, `MONITOR_LINK_SIO1` or
`MONITOR_LINK_FT232H`).

| Host | Build | Kernel | Link | Console text |
|------|-------|--------|------|--------------|
| OpenBIOS, DTL-H2700 | `openbios`: `make BOOT=cart MONITOR=1` | OpenBIOS | ATCONS word channel | ATCONS byte channel (OpenBIOS tty) |
| OpenBIOS, byte link | `openbios`: `make MONITOR=1 MONITOR_LINK=SIO1` or `FT232H`, `BOOT=cart` or `rom` | OpenBIOS | SIO1 or FT232H byte stream | Same stream, TTY mode |
| Retail PS-EXE | `monitor/hosts/retail` | Retail BIOS | SIO1 byte stream, or FT232H with `MONITOR_LINK=FT232H` | Same stream, TTY mode |
| Expansion ROM cart | `monitor/hosts/cart` | Retail BIOS | SIO1 byte stream | Same SIO1 stream, TTY mode |

The monitor uses only kernel interfaces that both kernels provide: the
table of tables at `0x100` and the A0/B0/C0 calls. It also patches a slot in
the kernel exception handler when it finds one it recognises (section 11),
and reads the 512 KiB BIOS region at `0xBFC00000` once, for the checksum in
HELLO and PONG (section 7).

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

- Framing: byte stream, section 2.4. Every frame word is two bytes, low byte
  first.
- Flow control, PS1 transmit: the SIO1 hardware gates TX on CTS, which is the
  host's RTS. Console text and frames from the PS1 stop while the host holds
  RTS low.
- Flow control, PS1 receive: the monitor raises its RTS only while it is
  inside a receive (from the start of a frame hunt to the end of the frame's
  checksum) and lowers it after. The hardware never drops RTS on its own. The
  8-byte FIFO absorbs a few bytes sent after RTS drops. The receive overrun
  bit is not checked.
- The STOP check in RUNNING (section 5) raises RTS while it drains the link,
  lowers it after, and then resets the SIO1 error flags, which clears the
  overrun an 11-byte STOP frame leaves in the 8-byte FIFO.
- A host that cannot observe the PS1's RTS must send commands only when the
  monitor is waiting for a frame: in HALTED after the response to the
  previous command. STOP in RUNNING is the one exception. Section 5 lists
  when each holds.
- RTS stays raised for the whole of a frame, so within a frame the monitor
  cannot hold the host back. This is why an LZ4 load needs a host-side cap on
  match length (section 6).
- Console text, PS1 -> host: `monitor/hosts/sio1-tty.c` replaces the kernel
  `tty` device and reopens stdin/stdout on it. Its writes go out on SIO1 as
  TTY-mode bytes; `0x00` bytes are dropped. Reads return nothing. Since the
  monitor is single-threaded, console bytes never land inside a frame.
- Console text, host -> PS1: non-zero bytes the monitor sees while hunting
  for a frame are counted and discarded. There is no stdin on this link.
- The retail and cart hosts do not call `monitorMain()`, so no banner is
  printed (section 12.5). OpenBIOS on a byte link prints the banner as
  console text on the link (section 12.2).
- Max LEN: `TRANSPORT_STREAM_MAX_LEN` = 4112 words (an 8 KiB payload plus 16
  header words).
- Checksum: mandatory. A received CKSUM of `0x00000000` fails as a checksum
  error.

### 2.3 FT232H byte stream (`MONITOR_LINK_FT232H`)

`link-ft232h.h` defines `MONITOR_LINK_IS_STREAM`. An FT232H on the expansion
port, its EEPROM set to CPU-style FIFO mode; the host sees a serial port
whose rate setting is ignored.

This link has not run on hardware yet.

| Register | Address | Width | Use |
|----------|---------|-------|-----|
| Data | `MONITOR_FT232H_DATA` (build-time) | u8 | Byte in, byte out |
| Status | `MONITOR_FT232H_STATUS` (build-time) | u8 | Bit 0: received byte waiting. Bit 1: room to send |

- Framing: byte stream, section 2.4. Every frame word is two bytes, low byte
  first.
- Addresses: `MONITOR_FT232H_BOARD` selects a preset from
  `monitor/hosts/ft232h-boards.mk` (`psx232h-a20`, `psx232h-a0`,
  `picodev-usb`, `picodev-uart`, `piodev-lite`), or `MONITOR_FT232H_DATA`,
  `MONITOR_FT232H_STATUS` and `MONITOR_FT232H_EXP1_CONFIG` are passed
  directly. With no setting the link uses `0xBF000000` / `0xBF100000` and
  EXP1 config `(23 << 16) | 0x2422`.
- Link init writes `MONITOR_FT232H_EXP1_CONFIG`, when defined, to the EXP1
  delay/size register (`0x1F801008`). Nothing else is initialised.
- Flow control: both directions poll the status bits. USB flow control holds
  the host back while the chip's buffers are full; the monitor has no RTS to
  drive.
- Console text: the same `sio1-tty.c` device and the same rules as SIO1
  (section 2.2).
- Max LEN: `TRANSPORT_STREAM_MAX_LEN` = 4112 words.
- Checksum: mandatory, as on SIO1.
- No line rate: SET_BAUD answers `ERROR(EBADCMD)`.

### 2.5 Stream framing (TTY mode and frame mode)

Applies to links that define `MONITOR_LINK_IS_STREAM` (SIO1, FT232H). Console
text and frames share one byte stream in each direction.

- TTY mode is the default. Every non-zero byte is console text.
- A `0x00` byte starts a frame. The sender emits exactly one `0x00`, then the
  frame words of section 3, SYNC first, each word low byte first.
- A run of `0x00` bytes is one frame start. The receiver skips the run and
  checks the two bytes after it against SYNC (`0xAA 0x55`).
- A byte that does not continue SYNC is examined again as the first byte of
  what follows: a `0x00` there starts the next frame, any other value is
  console text. This applies to the first byte after the run (which is
  non-zero, so console text) and to the byte after `0xAA`.
- After SYNC the receiver reads TYPE and LEN. A LEN above 4112 is treated as
  noise and nothing is sent. The TYPE and LEN bytes are not examined again;
  the byte after LEN is, as above.
- Otherwise the receiver consumes exactly LEN payload words and the two
  CKSUM words, then returns to TTY mode. Zero bytes inside a frame are data.
- The PS1 tty driver never emits `0x00`.

Wire bytes of a PING (no payload):

    00  AA 55  01 00  00 00  01 00 02 00
    |   SYNC   TYPE   LEN    CKSUM (0x00020001, low word first)
    frame start

Stream framing is implemented for the byte links only. The ATCONS link
carries frames on the word channel and console text on the byte channel
(section 2.1).

### 2.5 Line rate: SET_BAUD

SET_BAUD `[reload:u16]` changes the SIO1 reload. The sequence, as the monitor
runs it:

1. `reload == 0` -> `ERROR(EBADLEN)`. A link without a rate (ATCONS) ->
   `ERROR(EBADCMD)`. These checks run in that order.
2. The monitor sends ACK at the current rate, waits for the transmitter to
   drain (`TXEMPTY`), and writes the new reload.
3. Window 1: the monitor accepts only the exact bytes of a PING with no
   payload:
   `00 AA 55 01 00 00 00 01 00 02 00`.
   Any other byte is discarded. On a match it sends a short PONG at the new
   rate: LEN 1, payload `[proto_ver:u16]` only.
4. Window 2: the monitor accepts only the exact bytes of the confirmation
   PING, which carries the word 1:
   `00 AA 55 01 00 01 00 01 00 03 00 06 00`.
   On a match it sends a second short PONG (LEN 1, `[proto_ver:u16]`), keeps
   the new rate, and returns to the command loop.
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

The two window PONGs carry neither caps nor the BIOS checksum; a host that
needs them sends an ordinary PING after the switch. Outside the SET_BAUD
windows, PING with a payload is an ordinary PING: the payload is ignored,
and the reply is the full PONG of section 7.

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
The monitor always computes the checksum, so it never sends `0x00000000`. On
ATCONS a received `0x00000000` means "not computed" and is accepted without
verifying. On a byte link (SIO1, FT232H) the checksum is mandatory and a
received `0x00000000` fails.

Frame size limits:

| Frame | Limit |
|-------|-------|
| Any frame on SIO1 or FT232H | LEN <= 4112 |
| Any frame on ATCONS | LEN <= 65535 |
| Host commands other than WRITE_MEM and LOAD | LEN <= 16, else `ERROR(EBADLEN)` |
| WRITE_MEM and LOAD | LEN >= 4, else `ERROR(EBADLEN)` |
| WRITE_MEM and LOAD with `MON_LZ4` | LEN >= 10, else `ERROR(EBADLEN)` |
| Bulk data per frame, as the monitor sends it (DATA) | 8192 bytes |

## 4. Opcodes

The high nibble of the low byte of TYPE gives the direction: `0x0_` and
`0x2_` host -> PS1, `0x4_` PS1 response, `0x8_` PS1 event. Bit 15 of TYPE
(`MON_LZ4` = `0x8000`) is a flag valid only on WRITE_MEM and LOAD
(`0x8003`, `0x8008`); it marks the payload as LZ4 (section 6). On any other
opcode the flagged TYPE is unknown and answers `ERROR(EBADCMD)`.

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
| `0x0B` | STOP | HALTED: no-op, no reply. RUNNING: stops the target (section 5) |
| `0x0C` | STEP | Reserved; answers `ERROR(EBADCMD)` |
| `0x0D` | SET_BAUD | HALTED |

PS1 -> host:

| TYPE | Name | Payload |
|------|------|---------|
| `0x40` | ACK | none |
| `0x41` | DATA | READ_MEM data |
| `0x42` | REGS | GET_REGS data |
| `0x43` | PONG | `[proto_ver:u16][caps:u16][bios_fletcher32:u32]`; `[proto_ver:u16]` in the SET_BAUD windows |
| `0x4F` | ERROR | `[errcode:u16]` |
| `0x80` | HELLO | `[proto_ver:u16][caps:u16][bios_fletcher32:u32]` |
| `0x81` | STOPPED | Stop event |

`proto_ver` is `MON_PROTO_VER` = `0x0002`.

`caps` bits:

| Bit | Name | Meaning |
|-----|------|---------|
| 0 (`0x0001`) | `MON_CAP_LZ4` | WRITE_MEM and LOAD accept `MON_LZ4` (section 6) |
| 1 (`0x0002`) | `MON_CAP_STOP` | STOP is read while RUNNING (section 5). Set on SIO1 and FT232H |
| 2 (`0x0004`) | `MON_CAP_SLOT` | The monitor is entered from patch slot 4 of the kernel exception handler, ahead of the handler chains (section 11). Set only when the slot was patched |

The other bits are 0.

## 5. States

| State | Monitor activity | Link direction |
|-------|------------------|----------------|
| HALTED | Command loop, inside exception context | Host sends one command, monitor answers, repeat |
| RUNNING | Target executes; on each interrupt the monitor checks the link for a byte (`MON_CAP_STOP`) | PS1 -> host events; host -> PS1 STOP only |

Transitions:

| From | Trigger | To |
|------|---------|----|
| (entry) | `break 4, 1` at monitor start: HELLO | HALTED |
| HALTED | RUN, CONT (after ACK) | RUNNING |
| RUNNING | STOPPED event (software break, hardware breakpoint, watch, fault) | HALTED |
| RUNNING | STOP received, then the target's next interrupt: STOPPED INTERRUPT | HALTED |

- In HALTED the monitor emits no unsolicited frames.
- In RUNNING the host sends nothing but STOP, and STOP only when caps has
  `MON_CAP_STOP`. On ATCONS anything the host posts waits in the word
  channel and is read as the first command after the next stop.
- A PCDRV call or a program exit is a software break: the target stops, and
  the host serves it from HALTED (section 8).

STOP while RUNNING (`MON_CAP_STOP`):

- The monitor looks at the link only when the target takes an interrupt
  (ExcCode 0). With no byte waiting the interrupt goes to the kernel as
  usual.
- With a byte waiting, the monitor reads and drops every byte until the link
  has been quiet for `MONITOR_STOP_QUIET_SPINS` polls (build-time, default
  20000, about 7 ms on a retail CPU). If any of those bytes was `0x00`, the
  target stops with STOPPED INTERRUPT (section 7). Otherwise the bytes are
  dropped as console text and the interrupt goes on to the kernel.
- The frame is not parsed or validated: any `0x00` counts. The SIO1 RX FIFO
  holds 8 bytes and a STOP frame is 11
  (`00 AA 55 0B 00 00 00 0B 00 16 00`), so its tail may be lost by the time
  the interrupt comes.
- On SIO1 the monitor's RTS is low in RUNNING; the host sends STOP without
  waiting for it.
- A target that runs with interrupts disabled, or with every IRQ masked,
  cannot be stopped. The retail and cart loaders leave `I_MASK` at 0
  (section 12.5); RUN does not change it.
- The IRQ behind the interrupt the target stopped on is left pending and
  unacknowledged in `I_STAT`. After CONT the kernel and the target handle it
  as usual.
- A STOP that arrives after the target has already stopped for another
  reason is read in HALTED, where it is a no-op with no reply (section 6).

Command loop, per frame:

1. Hunt for a frame (section 2.1 or 2.4), read TYPE and LEN.
2. WRITE_MEM and LOAD: decode the payload straight into target memory, then
   verify the checksum (section 6). A frame with LEN < 4 is drained and
   answers `ERROR(EBADLEN)` whatever the checksum. With `MON_LZ4` set, the
   payload is fed to the LZ4 decoder instead; a monitor built without LZ4
   drains the frame and answers `ERROR(EBADCMD)` whatever the checksum.
3. Anything else: buffer up to 16 payload words, drain the rest, verify the
   checksum.
4. Checksum failure -> `ERROR(ECKSUM)`. Otherwise LEN > 16 ->
   `ERROR(EBADLEN)`. Otherwise dispatch. Unknown TYPE -> `ERROR(EBADCMD)`.

Apart from the WRITE_MEM and LOAD minimums, payload length is not checked
against the opcode. A command frame shorter than its layout is dispatched
with stale words from the previous command in the missing fields.

## 6. Commands

Each entry: request payload -> response. Offsets are in words.

| Command | Request payload | Response |
|---------|-----------------|----------|
| PING | none (payload ignored) | `PONG [proto_ver:u16][caps:u16][bios_fletcher32:u32]` |
| READ_MEM | `[addr:u32][len:u32]` | One or more DATA frames |
| WRITE_MEM | `[addr:u32][len:u32][bytes]` | ACK, `ERROR(EBADLEN)`, `ERROR(ECKSUM)` |
| LOAD | `[addr:u32][len:u32][bytes]` | ACK, `ERROR(EBADLEN)`, `ERROR(ECKSUM)` |
| WRITE_MEM, LOAD with `MON_LZ4` | `[dest:u32][rawlen:u32][clen:u32][off:u32][nbytes:u32][bytes]` | ACK, `ERROR(EBADLEN)`, `ERROR(EBADSTATE)`, `ERROR(ECKSUM)`, `ERROR(EDECODE)`, `ERROR(EBADCMD)` |
| GET_REGS | none | `REGS [38 x u32]` |
| SET_REG | `[idx:u16][value:u32]` | ACK, `ERROR(EBADREG)`, `ERROR(EBADSTATE)` |
| SET_BP | `[kind:u16][addr:u32][mask:u32]` | ACK, `ERROR(EBADCMD)` |
| CLR_BP | `[kind:u16]` | ACK |
| RUN | `[pc:u32][gp:u32][sp:u32]` | ACK, then RUNNING |
| CONT | none | ACK then RUNNING, or `ERROR(EBADSTATE)` |
| STOP | none | None in HALTED; STOPPED INTERRUPT in RUNNING (section 5) |
| SET_BAUD | `[reload:u16]` | Section 2.5 |

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
  8 KiB per frame (LEN 4100); that is required on the byte links and
  conventional on ATCONS.
- LEN < 4 (the payload does not hold the 4 header words): the frame is
  drained, nothing is written, and the answer is `ERROR(EBADLEN)` whether or
  not the checksum matched.
- `addr` is not validated.
- The frame marks memory as written: the next resume (RUN or CONT) flushes
  the instruction cache first. This holds for every WRITE_MEM or LOAD frame
  with LEN >= 4, and for every `MON_LZ4` frame on a monitor built with LZ4,
  including one that fails.

WRITE_MEM and LOAD with `MON_LZ4`:

- Available when the monitor sets `MON_CAP_LZ4` in HELLO and PONG. Builds
  with `MONITOR_LZ4` have it: every SIO1 build by default
  (`MONITOR_NO_LZ4` turns it off), ATCONS and FT232H builds only when
  `MONITOR_LZ4` is defined.
- The frames carry consecutive slices of one LZ4 block (raw block format, no
  LZ4 frame header) of `clen` bytes that decodes to `rawlen` bytes at
  `dest`. The payload header is 10 words, read in this order:

  | Words | Field | Meaning |
  |-------|-------|---------|
  | 0-1 | `dest:u32` | Where the decoded data goes |
  | 2-3 | `rawlen:u32` | Decoded length of the whole block |
  | 4-5 | `clen:u32` | Compressed length of the whole block |
  | 6-7 | `off:u32` | Offset of this slice in the compressed block |
  | 8-9 | `nbytes:u32` | Compressed bytes in this frame |
  | 10.. | bytes | `nbytes` bytes, packed low byte first |

  LEN = 10 + ceil(nbytes / 2). Bytes past `nbytes` in the payload are
  ignored.
- `off == 0` starts a new stream at `dest`, dropping any stream in flight.
  Any other `off` must equal the byte count the open stream has consumed,
  else `ERROR(EBADSTATE)`. `dest` is used only from the `off == 0` frame.
- LEN < 10, `nbytes` larger than the payload holds, or `off + nbytes > clen`
  -> `ERROR(EBADLEN)`. The frame is drained in every case.
- A checksum mismatch answers `ERROR(ECKSUM)` in place of any other error
  the frame produced.
- Bytes are fed to the decoder as they arrive, before the checksum is known,
  and the decoder writes straight to `dest`: there is no staging buffer. The
  match history is the output already in memory, so a match reaches at most
  64 KiB back and never before `dest`.
- Every frame is answered on its own. The frame that brings the consumed
  count to `clen` also ends the block and checks that exactly `rawlen` bytes
  were written, else `ERROR(EDECODE)`.
- Any error drops the stream; the host starts over from `off == 0`. Bytes
  already decoded stay in memory.
- The decoder is built with `LZ4STREAM_TRUSTED`: a malformed block (a zero
  offset, a match before `dest`, a block ending mid-sequence) is not
  detected, and `EDECODE` reports only a wrong decoded length.
- Pacing is the host's job. The decoder copies a match as its length bytes
  arrive: up to 19 bytes after the second offset byte, then up to 255 bytes
  after each extra length byte. On SIO1 the monitor cannot pause the host
  inside a frame (section 2.2) and the RX FIFO holds 8 bytes, so the host
  must cap the match length of every sequence so that the copy finishes
  before the following bytes overrun the FIFO. The cap is a host-side
  choice; the reference host re-encodes the block so no sequence copies more
  than 128 bytes. The monitor does not check it.
- A monitor built without LZ4 drains the frame and answers
  `ERROR(EBADCMD)`.

GET_REGS: section 9. With no halted context every slot is 0 except
BadVaddr. RUN and CONT both clear the halted context, so there is none in
HALTED before the first stop or after a `break 4, 1` entry (section 11).

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
- Clears the halted context, sends ACK, flushes the instruction cache if
  memory was written since the last resume, and returns from the exception
  into `pc`.
- Accepted in HALTED whether or not a program was stopped.

CONT: resumes the halted context, including any SET_REG changes, after the
same conditional instruction cache flush as RUN. It clears the halted
context as RUN does: GET_REGS, SET_REG and CONT see none until the next
stop. With no halted context -> `ERROR(EBADSTATE)`. The resume PC for each
stop reason is in section 11.

STOP: in HALTED it is a no-op and sends no reply. A STOP the host sent while
the target was stopping for another reason arrives in HALTED, after the
STOPPED; an ACK there would match no command the host is waiting on. While
RUNNING, STOP stops the target on links with `MON_CAP_STOP` (section 5).

## 7. Events

HELLO, LEN 4 (PONG outside the SET_BAUD windows has the same payload):

| Word | Field | Value |
|------|-------|-------|
| 0 | `proto_ver:u16` | `0x0002` |
| 1 | `caps:u16` | Capability bits (section 4) |
| 2-3 | `bios_fletcher32:u32` | Checksum of the BIOS region |

`bios_fletcher32` is a Fletcher-32 of the 512 KiB at `0xBFC00000`, computed
once before the monitor is entered (`monitorBiosChecksum()` in `install.h`).
It uses the frame checksum's sums (section 3): the region is read as
16-bit little-endian words in address order, `s1` and `s2` are 32-bit and
wrap, each is reduced mod 65535 at the end, and `s2` is the high half. The
frame checksum's substitution of `0xFFFFFFFF` for 0 does not apply. On the
DTL-H2700 the region is the flash image, which also holds the OpenBIOS
monitor at `0xBFC40000`.

HELLO is sent each time the monitor is entered through `break 4, 1`, which in
the shipped hosts is once, at start-up. On ATCONS a HELLO nobody has read
stays in the word channel: a host attaching later reads the pending HELLO
before its first PING, or it takes the HELLO for the PING's reply. On SIO1 a
HELLO sent before the host listens is lost; the host attaches with PING, and
the PONG carries the same fields.

STOPPED, LEN 7:

| Word | Field |
|------|-------|
| 0 | `reason:u16` |
| 1-2 | `epc:u32`, the resume PC of the halted context |
| 3-4 | `a:u32` |
| 5-6 | `b:u32` |

| reason | Name | a | b | epc |
|--------|------|---|---|-----|
| `0x01` | BREAKPOINT, software `break` | The `break` instruction word | 0 | The `break` itself |
| `0x01` | BREAKPOINT, hardware exec | 0 | 0 | The instruction |
| `0x02` | INTERRUPT, STOP while RUNNING | 0 | 0 | The interrupted instruction (the resume PC) |
| `0x03` | DATA_WATCH | Armed watch address (BDA) | 0 | The instruction |
| `0x04` | FAULT | ExcCode | BadVaddr for ExcCode 4, 5; else 0 | The faulting instruction |

Every STOPPED enters HALTED. The monitor sends no other stop reason; hosts
may still see reason `0x05` (EXIT) from older monitors.

## 8. PCDRV and exit

The monitor gives no meaning to a software `break` other than its own entry
(`break 4, 1`). Every other one stops with STOPPED BREAKPOINT, `a` = the
instruction word and `epc` on the `break`; the host decodes it:

    code1 = (a >> 16) & 0x3FF
    code2 = (a >> 6) & 0x3FF

| code1 | code2 | Meaning, by host convention |
|-------|-------|-----------------------------|
| 4 | 0 | Program exit, exit code in `a0` |
| 0 | `0x101`-`0x107` | PCDRV call |
| any other | any other | Plain breakpoint |

Exit: the host reads the code from `a0` (GET_REGS index 4). CONT would
re-execute the `break 4, 0` and stop again.

PCDRV calls, as issued by `common/kernel/pcdrv.h`:

| code2 | Call | Arguments | Result |
|-------|------|-----------|--------|
| `0x101` | PCinit | - | v0 = ret |
| `0x102` | PCcreat | a0 = name, a2 = mode (the wrapper passes 0) | v0 = 0, v1 = ret |
| `0x103` | PCopen | a0 = name, a2 = flags | v0 = 0, v1 = ret |
| `0x104` | PCclose | a0 = fd | v0 = ret |
| `0x105` | PCread | a1 = fd, a2 = len, a3 = buf | v0 = 0, v1 = ret |
| `0x106` | PCwrite | a1 = fd, a2 = len, a3 = buf | v0 = 0, v1 = ret |
| `0x107` | PClseek | a0 = fd, a2 = offset, a3 = whence | v0 = 0, v1 = ret |

The `pcdrv.h` wrappers return v1 when v0 is 0, else -1. `ret` is the host's
result: a file descriptor, a byte count, a new position, or a negative value
for failure. For the calls whose result is in v1, a non-zero v0 also makes
the wrapper return -1.

Serving a PCDRV call from HALTED:

1. GET_REGS; take a0-a3 from indices 4-7.
2. PCcreat, PCopen: READ_MEM the name at a0 up to its NUL.
3. PCwrite: READ_MEM `len` bytes at `buf`. PCread: perform the read on the
   host, then WRITE_MEM the bytes to `buf`.
4. SET_REG 2 (v0) and SET_REG 3 (v1) with the result, as the table gives.
5. SET_REG 37 (PC) to `epc + 4`, past the `break`.
6. CONT.

A WRITE_MEM made while serving a call makes the CONT flush the instruction
cache (section 6).

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
| `0x01` | EBADCMD | Unknown TYPE (including STEP, and `MON_LZ4` on any opcode but WRITE_MEM and LOAD); SET_BP kind > 3; SET_BAUD on a link without a rate; `MON_LZ4` on a monitor built without LZ4 |
| `0x02` | EBADSTATE | SET_REG or CONT with no halted context; LZ4 slice with `off != 0` that does not continue the open stream |
| `0x03` | EBADADDR | Defined, never sent |
| `0x04` | EBADREG | SET_REG idx > 37 |
| `0x05` | EBADLEN | Command payload over 16 words (not WRITE_MEM/LOAD); WRITE_MEM/LOAD frame under 4 words; SET_BAUD reload 0; LZ4 frame under 10 words, `nbytes` beyond the payload, or `off + nbytes > clen` |
| `0x06` | ECKSUM | Checksum mismatch on a command frame (on SIO1 and FT232H, also a CKSUM of 0) |
| `0x07` | ENOFD | Defined, never sent |
| `0x08` | EDECODE | LZ4 stream complete but not exactly `rawlen` bytes decoded |

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

For kinds 1-3 the monitor sets `kind * DR`: bit 0 of kind selects DR, bit 1
selects DW. It also records `addr` as the watch address that DATA_WATCH
reports.

| DCIC bit | Name |
|----------|------|
| 2 | DA, status: the last debug break was a data access |
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
- A hardware stop disarms the whole debug unit: DCIC is written 0 and the
  monitor's DCIC image is cleared, so the exec and the data breakpoint are
  both off, whichever one fired. BPC, BPCM, BDA and BDAM keep their values.
  The host re-arms with SET_BP before CONT.
- The stop reason of a hardware stop is decided by DCIC status bit 2 (DA) as
  read at the stop: DA set is DATA_WATCH, DA clear is BREAKPOINT. The enable
  bits play no part, so with both kinds armed an exec hit reports
  BREAKPOINT.
- The monitor copies the general exception trampoline at `0x80` to the
  cop0 break vector at `0x40`, so both vectors reach the same handler.

Exception entry. The monitor is reached by two routes:

1. Patch slot 4 of the kernel exception handler (handler + `0xA0`), when
   the slot was patched (`MON_CAP_SLOT`, section 12.6). The slot runs after
   the kernel has saved at, v0, v1, ra and the resume PC, and before any
   handler chain. It holds `lui at / ori at, at / jalr at / nop`, a call to
   the monitor's slot entry. For an exception the table below keeps, and
   for an interrupt with a byte waiting on a link with `MON_CAP_STOP`, the
   slot entry saves the rest of the context into the thread's register frame
   in the same order and at the same offsets as the kernel, switches to its
   own stack and runs the dispatch. Anything else returns straight to the
   kernel. An interrupt the dispatch declines (console text only, or no
   target running) returns to the kernel with every register the dispatch
   may have changed put back; the kernel then runs its chains.
2. The priority-0 handler chain, ahead of the kernel's syscall handler. It
   stays registered whether or not the slot was patched, and runs the same
   dispatch after the kernel has saved the whole context. It is lost if a
   program resets the priority-0 chain; the slot is not.

An exception the slot entry keeps never reaches the chains, so none is
handled twice.

Exception dispatch:

| ExcCode | Condition | Action |
|---------|-----------|--------|
| 9 (BP) | Word at resume PC has function field `0x0D` (`break`) | Software break, decoded below |
| 9 (BP) | Otherwise | Hardware breakpoint: DCIC written 0; DATA_WATCH if DCIC status bit 2 (DA) was set, else BREAKPOINT |
| 4, 5 (AdEL, AdES) | - | FAULT, a = ExcCode, b = BadVaddr |
| 6, 7, 10, 11, 12 (IBE, DBE, RI, CpU, Ov) | - | FAULT, a = ExcCode, b = 0 |
| 0 (Int) | RUNNING, a byte waiting on the link, and a `0x00` among the bytes drained (section 5) | STOPPED INTERRUPT, a = 0, b = 0 |
| 0 (Int) | Otherwise | Not handled; passed to the kernel |
| 8, others | - | Not handled; passed to the kernel |

Software `break code1, code2` (code1 = bits 25:16, code2 = bits 15:6):

| code1 | code2 | Action |
|-------|-------|--------|
| 4 | 1 | Monitor entry (word `0x0004004D` exactly): HELLO, then the command loop, with no halted context (section 6) |
| any other | any other | STOPPED BREAKPOINT, a = the instruction word, resume PC left on the break |

What a break other than `break 4, 1` means (exit, PCDRV, breakpoint) is
decided by the host (section 8).

Resume behaviour with CONT:

- Software break: re-executes the `break` and stops again. To continue after
  it, the host sets PC (SET_REG 37) to `epc + 4` first.
- Hardware breakpoint or watch: re-executes the instruction; the debug unit
  is already disarmed, so it does not trap again.
- FAULT: re-executes the faulting instruction unless the host changes PC
  (SET_REG 37).

STEP (`0x0C`) is reserved and answers `ERROR(EBADCMD)`. Single-stepping can
be built on the host from GET_REGS, READ_MEM and a one-shot exec breakpoint
at the computed next PC; no protocol change is needed for that.

## 12. Bootstrap

### 12.1 OpenBIOS on the DTL-H2700 (ATCONS)

- Build: `make BOOT=cart MONITOR=1` in `openbios/`, with `MONITOR_LINK` at
  its default, `ATCONS`. This defines
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

### 12.2 OpenBIOS on a byte link (SIO1, FT232H)

- Build: `make MONITOR=1 MONITOR_LINK=SIO1` or `MONITOR_LINK=FT232H` in
  `openbios/`, with `BOOT=cart` (`psx-bios-as-cart.ld`) or `BOOT=rom`
  (`psx-bios.ld`, the default). This defines `OPENBIOS_MONITOR` and
  `MONITOR_LINK_SIO1` or `MONITOR_LINK_FT232H`, strips the shell and CD-ROM
  boot, and links OpenBIOS where that `BOOT` setting normally puts it.
  `MONITOR_SIO1_RELOAD` (default 18) sets the initial SIO1 rate;
  `MONITOR_FT232H_BOARD` selects the FT232H addresses (section 2.3).
- OpenBIOS cold-initialises as the kernel. In place of the shell and game
  boot it initialises the link, installs `sio1-tty.c` as the kernel `tty`
  device so console text goes out on the link, and calls `monitorMain()`.
  The banner goes out as console text.

### 12.3 Retail PS-EXE (SIO1 or FT232H)

- Build: `monitor/hosts/retail`, a PS-EXE loader linked at `0x801C0000`
  (`MONITOR_TLOAD`) that carries the resident core (section 12.5).
  `MONITOR_LINK` is `SIO1` (default) or `FT232H`, with
  `MONITOR_FT232H_BOARD` (section 2.3). `MONITOR_SIO1_RELOAD` sets the
  initial SIO1 rate. `MONITOR_NO_SLOT=1` leaves the exception handler's
  patch slot alone (section 12.6).
- Start by any means that runs a PS-EXE on a retail BIOS:
  - `make iso` builds a disc image with the EXE as `PSX.EXE` and no
    `SYSTEM.CNF` (needs `exe2iso`; the disc has no license data, so the
    console must boot unlicensed discs).
  - A SIO1 loader already on the console (for example Unirom) uploads and
    runs the EXE. The monitor then takes over SIO1.
- `main()` installs the core and enters the monitor (section 12.5), except
  on OpenBIOS with API version 1 or later (section 12.8):
  - OpenBIOS with the monitor built in: nothing is copied or installed;
    `main()` executes `break 4, 1` before touching interrupts, and the
    OpenBIOS monitor answers with HELLO.
  - OpenBIOS without it: the core's link range `[__core_start, __core_end)`
    must lie inside `getCodeCave()`. If it does not, the loader prints both
    ranges on the kernel tty and a line of console text on the link, and
    halts.

### 12.4 Expansion ROM cart (SIO1)

- Build: `monitor/hosts/cart` builds a PS-EXE loader linked at `0x801C0000`
  that carries the retail host's core (section 12.5), and wraps it with
  `rom.s` into an image linked at `0x1F000000` (EXP1).
- The image holds the license strings at `0x04` and `0x84` and the
  pre-boot entry pointer at `0x80`. The BIOS calls the pre-boot entry before
  the kernel is set up. The pre-boot code copies a jump to `start` into the
  `0x40` debug vector, arms a cop0 data-write breakpoint on `0x80030000`
  (DCIC `0xEB800000`), and returns.
- When the BIOS writes the shell to `0x80030000` with the kernel up, the
  breakpoint fires; `start` disarms it, copies the appended PS-EXE to its
  load address, and jumps to its entry, still inside the exception.
- `main()` then enters the critical section, masks and acknowledges all
  IRQs, restores the default exception return, and installs the core and
  enters the monitor as the retail loader does (section 12.5). The cart host
  is SIO1 only. `MONITOR_NO_SLOT=1` applies as for the retail host.
- `monitor/tools/cartflash` programs an Am29F010 cart flash with a payload,
  run as a target under the monitor.

### 12.5 Resident core (retail and cart)

The retail and cart hosts run in two stages: a loader, then a resident core.

- `monitor/hosts/retail/core` builds the core: the monitor, the transport
  and the `sio1-tty.c` tty device, all built for the selected link.
  `core.ld` links it into `0x8000C160` up to `0x8000DF80`, the RAM between
  the end of the retail kernel's bss and the BIOS patch area at
  `0x8000DF80`. Its bss is folded into the image as zeroes, so copying the
  image is the whole install.
- Both loaders embed the core image and link against the core's ELF for its
  symbols.
- The loader's `main()`: enters the critical section, writes 0 to `I_MASK`
  and `I_STAT`, draws the OpenBIOS colour bars (polling VBLANK in `I_STAT`),
  copies the core to `0x8000C160` and flushes the instruction cache,
  computes the BIOS checksum, initialises the link, installs the tty device,
  hooks the kernel (section 12.6), and executes `break 4, 1`.
- Interrupts stay off: the loader never leaves the critical section and
  leaves `I_MASK` at 0. A target gets its SR from RUN and unmasks the IRQs
  it uses itself.
- From then on the monitor runs from the core alone. The loader is not used
  again and its RAM is free for targets.
- The loader does not call `monitorMain()`: there is no banner, and the
  monitor's state starts from the core's zeroed bss.

### 12.6 Monitor start (OpenBIOS) and kernel hook

OpenBIOS calls `monitorMain()`:

1. Prints the banner on the kernel tty (the ATCONS byte channel, or console
   text on a byte link).
2. Initialises the link.
3. Clears the halted context, BadVaddr, DCIC image and watch address.
4. Computes the BIOS checksum for HELLO and PONG (section 7).
5. Hooks the kernel (below).
6. Executes `break 4, 1`: the handler sends HELLO and enters the command
   loop in exception context.

Hooking the kernel (`monitorHook()` in `install.h`, shared by OpenBIOS and
the retail and cart loaders):

1. Installs the monitor's handler at priority 0 on the kernel's chain
   (`SysEnqIntRP(0, ...)`).
2. Copies the four words of the `0x80` trampoline to `0x40` and flushes the
   instruction cache.
3. Unless built with `MONITOR_NO_SLOT`, finds patch slot 4 of the kernel
   exception handler, writes `lui at, hi / ori at, at, lo / jalr at / nop`
   there (a call to the monitor's slot entry), flushes the instruction
   cache, and records the slot, which sets `MON_CAP_SLOT`.

Finding the slot:

- Monitor built into OpenBIOS (sections 12.1, 12.2): OpenBIOS's own
  `exceptionHandlerPatchSlot4`, with no further check.
- Retail loader on OpenBIOS with API version 1 or later: OpenBIOS writes
  the slot, through `installExceptionSlot(4, ...)` (section 12.8).
- Otherwise, retail and cart loaders: the handler address comes from B0 `0x56`
  (GetC0Table), entry 6, on a retail kernel. On OpenBIOS it is decoded from
  the `0x80` vector (`lui k0, hi / addiu k0, k0, lo / jr k0`) instead,
  because OpenBIOS's GetC0Table checks the code at its caller against known
  patch hashes and halts the machine on an unknown one ("Couldn't find C0
  patch hash ... Stopping."). If the vector is not that sequence, no slot.
- The slot is used only if the handler is word-aligned in main RAM, the word
  at handler + `0x6C` is `0xAF430080` (`sw v1, 0x80(k0)`), the word at
  handler + `0xB0` is `0xAF440010` (`sw a0, 0x10(k0)`), and the four words
  at handler + `0xA0` are all 0 (nops). Otherwise the monitor runs on the
  priority-0 chain alone and `MON_CAP_SLOT` is clear.

### 12.7 Host session

1. Attach: on ATCONS read a pending HELLO if one is there; on SIO1 listen at
   the build's initial rate; on FT232H open the serial port at any rate.
   PING until PONG. HELLO or PONG gives the protocol version, the caps and
   the BIOS checksum.
2. On SIO1, optionally SET_BAUD (section 2.5).
3. LOAD the program in 8 KiB frames (LZ4 slices when caps has
   `MON_CAP_LZ4`), then RUN with its entry PC, gp and sp.
4. While RUNNING: collect console text, wait for STOPPED. With
   `MON_CAP_STOP`, a STOP frame halts the target at its next interrupt.
5. After STOPPED: decode a software break (section 8); serve a PCDRV call
   and CONT, or take an exit. Otherwise inspect or modify with READ_MEM,
   WRITE_MEM, GET_REGS, SET_REG, SET_BP, CLR_BP; then CONT, or LOAD and RUN
   the next program.

### 12.8 OpenBIOS API

OpenBIOS stores its API entry in A0 table entry `0x0B` with bit 0 set; a
retail kernel has a word-aligned function pointer there. A caller jumps to
the entry with bit 0 cleared and the function index in `t1`. Wrappers are in
`common/kernel/openbios.h`; each returns 0, -1 or NULL without calling
anything when OpenBIOS is absent or older than the version that added it.

| Index | Call | Version | Returns |
|-------|------|---------|---------|
| 0 | `getOpenBiosApiVersion()` | 0 | the API version (1) |
| 1 | `getOpenBiosBuildId()` | 0 | the build id |
| 2 | `getMonitor()` | 1 | nonzero if the monitor is built in |
| 3 | `installExceptionSlot(slot, fn)` | 1 | 0, or nonzero with nothing written |
| 4 | `getCodeCave(&size)` | 1 | `0x8000C160`, size `0x1E20` |

- `installExceptionSlot` takes slot 1 to 4. If the slot is four nops it
  writes `lui at, hi / ori at, at, lo / jalr at / nop` calling `fn`, with
  interrupts masked, then flushes the instruction cache.
- The code cave is `0x8000C160..0x8000DF80`. OpenBIOS never uses it, and its
  link scripts fail the build if its kernel data would reach it.
- The dispatcher does not bound `t1`: an index past the table of the running
  version jumps through whatever follows it. Check the version first.

## 13. Not implemented

- STOP on ATCONS. The monitor does not read the word channel while a
  target runs; a running target stops only on its own.
- STOP frame validation. In RUNNING any `0x00` byte on a byte link stops
  the target; the frame's TYPE, LEN and checksum are not checked
  (section 5).
- STEP (`0x0C`).
- NAK and retransmission. A checksum failure is reported with
  `ERROR(ECKSUM)`; nothing is resent by the monitor.
- Address validation: EBADADDR is never sent; READ_MEM, WRITE_MEM and LOAD
  access whatever address they are given.
- ENOFD is never sent.
- LOAD extent bookkeeping: LOAD behaves as WRITE_MEM.
- Console input on the byte links: host -> PS1 console bytes are discarded.
- Stream framing on ATCONS: the ATCONS link uses the word channel only.
- SIO1 receive overrun detection.
- LZ4 stream validation beyond the decoded length (section 6).
- Cause.BD handling. For an exception in a branch delay slot the resume PC
  is the branch, and the monitor decodes the word there, so a `break` in a
  delay slot is not recognised as a software break: it is handled as a
  hardware breakpoint (DCIC written 0, STOPPED BREAKPOINT with a = 0).
- Instruction check: any word whose low 6 bits are `0x0D` counts as a
  `break`; the primary opcode is not checked.
