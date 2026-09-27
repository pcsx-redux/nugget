# Monitor kernel hooks (protocol v3, not implemented)

The design for making each of the monitor's kernel hooks selectable per
program. Nothing here is implemented; `PROTOCOL.md` describes the code and
takes this in when it lands.

Today every hook the monitor installs is fixed at build or install time
(`monitorHook()` in `install.h`, `installSio1Tty()` in the hosts), and no
command changes one. An arcade host (System 573, GV) adds hooks such as a
watchdog kick for binaries that do not handle the watchdog themselves, and a
user testing their own watchdog handling or their own kernel patches must be
able to turn those off. With every hook off the monitor is a serial loader
that does not stay resident after RUN.

## Hooks

Each hook is an install/remove pair. Install saves what it replaces, and
remove puts it back.

| Bit | Name | On | Off |
|-----|------|----|-----|
| 0 (`0x0001`) | `MON_HOOK_RESIDENT` | Chain entry at priority 0, patch slot 4 when `MON_CAP_SLOT`, software breaks (PCDRV, exit), STOP | Detach at RUN (below) |
| 1 (`0x0002`) | `MON_HOOK_TTY` | Kernel `tty` device is `sio1-tty.c`, console text on the link | Kernel's original `tty` device |
| 2 (`0x0004`) | `MON_HOOK_COP0` | `0x40` routes to the monitor: hardware breakpoints stop the target | Original words at `0x40`; a cop0 break goes wherever the target sends it |
| 3 (`0x0008`) | `MON_HOOK_FAULT` | Faults stop the target with STOPPED FAULT | Faults pass down the chain to the kernel |
| 4 (`0x0010`) | `MON_HOOK_WATCHDOG` | Reserved for arcade hosts: the monitor kicks the watchdog while RUNNING | No kick while RUNNING |

What gets saved:

- TTY: the kernel's `tty` device, before `installSio1Tty()` replaces it.
  Only hosts that install `sio1-tty.c` have this bit: the retail and cart
  hosts, and OpenBIOS on a byte link. On ATCONS the console is OpenBIOS's
  own tty driver and is not a monitor hook.
- COP0: the four words at `0x40`, before the `0x80` trampoline is copied
  over them.
- RESIDENT: the four nops of slot 4. The chain entry needs nothing saved;
  `SysDeqIntRP(0, ...)` removes it.

## Wire changes

- `MON_PROTO_VER` becomes `0x0003`.
- HELLO and PONG gain a word: `[proto_ver:u16][caps:u16][bios_fletcher32:u32][hooks:u16]`,
  LEN 5. `hooks` is the set of bits this host can honour. The SET_BAUD
  window PONGs are unchanged.
- RUN becomes `[pc:u32][gp:u32][sp:u32][hooks:u16]`. The word is required:
  a RUN without it answers `ERROR(EBADLEN)`. There is no default.
- New error `0x09` EHOOK: RUN sets a bit that is not in the host's `hooks`.

## RUN

Checked before anything changes, in this order:

1. A bit outside the host's `hooks`: `ERROR(EHOOK)`.
2. `MON_HOOK_COP0` or `MON_HOOK_FAULT` set with `MON_HOOK_RESIDENT` clear:
   `ERROR(EHOOK)`. Both need the monitor resident.
3. `MON_HOOK_COP0` clear while SET_BP has a breakpoint armed:
   `ERROR(EBADSTATE)`. The breakpoint would never reach the monitor.

Then each hook is installed or removed to match the word, the instruction
cache is flushed, and RUN proceeds as in v2. The hooks stay as RUN set them
until the next RUN; CONT does not change them.

With `MON_HOOK_RESIDENT` set, CONT, STOP and every stop behave as in v2.

## Detach (`MON_HOOK_RESIDENT` clear)

1. ACK.
2. Remove every hook: kernel `tty` device, `0x40`, slot 4, chain entry.
   Flush the instruction cache.
3. On an arcade host, kick the watchdog.
4. Jump to `pc` with the register state RUN sets.

After the ACK the host gets no more frames. It ends the session; a later
session needs the monitor started again. The link is left as the monitor
configured it. On the retail and cart hosts the core's RAM
(`0x8000C160`..`0x8000DF80`, section 12.5 of `PROTOCOL.md`) is free for the
program.

## Watchdog

The command loop runs in exception context for as long as the host takes
between commands, so an arcade host kicks the watchdog while HALTED whatever
the bits say. `MON_HOOK_WATCHDOG` governs RUNNING only. In a detach the last
kick is just before the jump, so the program starts with the full timeout.
