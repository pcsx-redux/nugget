# Monitor kernel hooks (protocol v3, not implemented)

The design for choosing, per program, which kernel patches the monitor
installs and which of its features are active. Nothing here is implemented;
`PROTOCOL.md` describes the code and takes this in when it lands.

Today every patch the monitor installs is fixed at build or install time
(`monitorHook()` in `install.h`, `installSio1Tty()` in the hosts), and every
feature is always on. An arcade host (System 573, GV) adds more, such as a
watchdog kick for binaries that do not handle the watchdog themselves, and a
user testing their own watchdog handling or their own kernel patches must be
able to turn them off. With every patch off the monitor is a serial loader
that does not stay resident after RUN.

## Patches

What the monitor installs. Each is an install/remove pair; install saves
what it replaces and remove puts it back.

| Bit | Name | Installs | Saved |
|-----|------|----------|-------|
| 0 (`0x0001`) | `MON_PATCH_KERNEL_CHAIN` | The priority-0 chain entry (`SysEnqIntRP(0, ...)`) | Nothing; `SysDeqIntRP(0, ...)` removes it |
| 1 (`0x0002`) | `MON_PATCH_KERNEL_SLOT` | Patch slot 4 of the kernel exception handler | The slot's four nops |
| 2 (`0x0004`) | `MON_PATCH_COP0_VECTOR` | The `0x80` trampoline copied to `0x40` | The four words at `0x40` |
| 3 (`0x0008`) | `MON_PATCH_TTY` | `sio1-tty.c` as the kernel `tty` device | The kernel's `tty` device |

`MON_PATCH_KERNEL_SLOT` is available only where the slot probe (section
12.6 of `PROTOCOL.md`) found the slot. `MON_PATCH_TTY` is available only on
hosts that install `sio1-tty.c`: the retail and cart hosts, and OpenBIOS on
a byte link. On ATCONS the console is OpenBIOS's own tty driver and is not a
monitor patch.

## Features

What the monitor does with the patches. A feature that is off declines its
exceptions: the kernel handles them as if the monitor were not there.

| Bit | Name | Stops the target on |
|-----|------|---------------------|
| 0 (`0x0001`) | `MON_FEAT_EXIT` | `break 4, 0` |
| 1 (`0x0002`) | `MON_FEAT_PCDRV` | `break 0, 0x101`..`0x107` |
| 2 (`0x0004`) | `MON_FEAT_BREAKPOINT` | Any other software `break`, and cop0 hardware breakpoints |
| 3 (`0x0008`) | `MON_FEAT_CPU_FAULT` | AdEL, AdES, IBE, DBE, RI, CpU, Ov |
| 4 (`0x0010`) | `MON_FEAT_STOP` | An interrupt with the host's STOP on the link (`MON_CAP_STOP`) |
| 5 (`0x0020`) | `MON_FEAT_WATCHDOG` | Reserved for arcade hosts: kick the watchdog while RUNNING |

`break 4, 1`, the monitor's own entry, is always taken.

Code changes this needs beyond the patches' remove paths:

- `monitorTake()` decodes the `break` code fields. Today every software
  break except `break 4, 1` stops the target and the host tells exit from
  PCDRV.
- The slot entry keeps exceptions by the constant `MON_SLOT_EXCMASK`. That
  becomes a word computed from the features at RUN.

Open: what each kernel does with a `break` nobody claims. If it returns to
EPC, a program running with `MON_FEAT_PCDRV` off and no break handler of its
own hangs on its first PCDRV call.

## Wire changes

- `MON_PROTO_VER` becomes `0x0003`.
- HELLO and PONG become
  `[proto_ver:u16][caps:u16][bios_fletcher32:u32][patches:u16][features:u16]`,
  LEN 6: the patches and features this host supports. The SET_BAUD window
  PONGs are unchanged.
- RUN becomes `[pc:u32][gp:u32][sp:u32][patches:u16][features:u16]`. Both
  words are required: a RUN without them answers `ERROR(EBADLEN)`. There is
  no default.
- New error `0x09` EHOOK.

## RUN

Checked before anything changes:

1. A bit the host does not support: `ERROR(EHOOK)`.
2. Any feature set with neither `MON_PATCH_KERNEL_CHAIN` nor
   `MON_PATCH_KERNEL_SLOT`: `ERROR(EHOOK)`. Every feature above is reached
   through an exception.
3. A hardware breakpoint armed by SET_BP with `MON_PATCH_COP0_VECTOR` or
   `MON_FEAT_BREAKPOINT` clear: `ERROR(EBADSTATE)`.

Then each patch is installed or removed to match, the instruction cache is
flushed, and RUN proceeds as in v2. Patches and features stay as RUN set
them until the next RUN; CONT does not change them.

Check 2 holds for every feature listed. A later feature that needs no
exception route (console output split from input, for example) replaces it
with a per-feature table of required patches.

## Detach (every patch clear)

1. ACK.
2. Remove every patch. Flush the instruction cache.
3. On an arcade host, kick the watchdog.
4. Jump to `pc` with the register state RUN sets.

After the ACK the host gets no more frames and ends the session. The link
is left as the monitor configured it. On the retail and cart hosts the
core's RAM (`0x8000C160`..`0x8000DF80`, section 12.5 of `PROTOCOL.md`) is
free for the program.

## Watchdog

The command loop runs in exception context for as long as the host takes
between commands, so an arcade host kicks the watchdog while HALTED whatever
the flags say. `MON_FEAT_WATCHDOG` governs RUNNING only. In a detach the
last kick is just before the jump, so the program starts with the full
timeout.
