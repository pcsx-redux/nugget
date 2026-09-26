BOOT ?= rom
BOARD ?= psx

TARGET = openbios
TYPE = bin

SRCS = \
../common/psxlibc/fastmemset.s \
boot/$(BOARD).s \
card/backupunit.c \
card/device.c \
cdrom/cdrom.c \
cdrom/events.c \
cdrom/filesystem.c \
cdrom/helpers.c \
cdrom/statemachine.c \
charset/sjis.c \
fileio/filesystem.c \
fileio/misc.c \
fileio/stdio.c \
gpu/gpu.c \
handlers/irq.c \
handlers/setup.c \
handlers/syscall.c \
kernel/alloc.c \
kernel/events.c \
kernel/flushcache.s \
kernel/handlers.c \
kernel/libcmisc.c \
kernel/memory-c.c \
kernel/memory-s.s \
kernel/misc.c \
kernel/qsort.c \
kernel/psxexe.c \
kernel/psxexec.s \
kernel/setjmp.s \
kernel/threads.c \
kernel/util.c \
kernel/vectors.s \
kernel/xprintf.c \
main/main.c \
main/splash.c \
pio/pio.c \
shell/shell.c \
sio0/busyloop.s \
sio0/card.c \
sio0/cardfasttrack.s \
sio0/driver.c \
sio0/pad.c \
tty/tty.c \
\
patches/hash.c \
patches/patches.c \
\
patches/clear_card_1.c \
patches/custom_handler_1.c \
patches/initgun_1.c \
patches/patch_card_info_1.c \
patches/patch_card_1.c \
patches/patch_card_2.c \
patches/patch_card2_1.c \
patches/patch_card2_2.c \
patches/patch_gte_1.c \
patches/patch_gte_2.c \
patches/patch_gte_3.c \
patches/patch_pad_1.c \
patches/patch_pad_2.c \
patches/patch_pad_3.c \
patches/remove_ChgclrPAD_1.c \
patches/remove_ChgclrPAD_2.c \
patches/send_pad_1.c \
patches/send_pad_2.c \
\
../shell/shell_data.o \
\

# Ensure all files are always cleaned up.
CLEANSRCS = \
boot/psx.s \
boot/system573.s \
boot/zn1.s \
boot/zn2.s \
\
font1.o \
font2.o \
./psexe_data.o \

ifeq ($(MAKECMDGOALS),clean)
SRCS += $(CLEANSRCS)
endif
ifeq ($(MAKECMDGOALS),deepclean)
SRCS += $(CLEANSRCS)
endif

CPPFLAGS =

ifneq ($(BOARD),psx)
# Disable cheat cart builds and CD-ROM/shell support for arcade boards.
BOOT = rom
BOOT_MODE = psexe
ifeq ($(BOARD),system573)
CPPFLAGS += -DOPENBIOS_BOARD_SYS573
else ifeq ($(BOARD),zn1)
CPPFLAGS += -DOPENBIOS_BOARD_ZN1
else ifeq ($(BOARD),zn2)
CPPFLAGS += -DOPENBIOS_BOARD_ZN2
endif
endif

ifeq ($(BOOT),rom)
LDSCRIPT = $(ROOTDIR)openbios/psx-bios.ld
SRCS += font1.o font2.o
else
LDSCRIPT = $(ROOTDIR)openbios/psx-bios-as-cart.ld
endif

CPPFLAGS += -DXPRINTFNOALLOC

ifneq ($(EMBED_PSEXE),)
CPPFLAGS += -DOPENBIOS_USE_EMBEDDED_PSEXE
SRCS += ./psexe_data.o
endif

ifneq ($(FASTBOOT),)
BOOT_MODE ?= fast
endif

ifeq ($(BOOT_MODE),fast)
CPPFLAGS += -DOPENBIOS_BOOT_MODE_NO_SHELL
else ifeq ($(BOOT_MODE),psexe)
CPPFLAGS += -DOPENBIOS_BOOT_MODE_NO_SHELL -DOPENBIOS_BOOT_MODE_NO_CDROM
endif

ifneq ($(SPLASH_SCREEN),)
CPPFLAGS += -DOPENBIOS_SHOW_SPLASH_SCREEN
endif

ifneq ($(INSTALL_TTY_CONSOLE),)
CPPFLAGS += -DOPENBIOS_INSTALL_TTY_CONSOLE
endif

# H2x00 resident debug monitor. Relinks OpenBIOS into a code cave of the official
# H2700 flash for now, other boards TBD. The H2700 code cave is 128kB large, and
# located at 0xbfc40000. The current build is slightly less than 64kB, which more
# than fits. _h2700_hook is the monitor's entry point, and
# h2700/flash/mkimage.py redirects the stock BIOS entry jump to it. It
# reads the reset-mode byte and boots openbios if it is 7, otherwise it falls
# through to the official BIOS.
#
# The build will strip all cdrom code, display an inverted splash screen to distinguish
# from the official one, and will activate the tty console on the atcons device.
# The monitor will start immediately and listen for commands on the wide atcons port.
#
# Requires a cart build (BOOT=cart, i.e. BOOT != rom, which
# selects psx-bios-as-cart.ld). Build with: make BOOT=cart MONITOR=1
ifneq ($(MONITOR),)
CPPFLAGS += -DOPENBIOS_H2X00_MONITOR -DMONITOR_LINK_ATCONS
# The H2700 ATCONS block needs EXP2 kept 16-bit, see boot/psx.s.
ASFLAGS += -Wa,--defsym,OPENBIOS_H2X00_MONITOR=1
CPPFLAGS += -DOPENBIOS_SHOW_SPLASH_SCREEN
CPPFLAGS += -DOPENBIOS_BOOT_MODE_NO_SHELL -DOPENBIOS_BOOT_MODE_NO_CDROM
CPPFLAGS += -DOPENBIOS_INSTALL_TTY_CONSOLE
# Link target defaults to the H2700 flash code-cave. Override on the make command
# line to relink the monitor elsewhere - e.g. MONITOR_ROM_BASE=0x80020000
# MONITOR_ENTRYPOINT=_reset for a main-RAM build that runs under the emulator.
MONITOR_ROM_BASE ?= 0xbfc40000
MONITOR_ROM_SIZE ?= 0x20000
MONITOR_ENTRYPOINT ?= _h2700_hook
LDFLAGS += -Wl,--defsym=ROM_BASE=$(MONITOR_ROM_BASE) -Wl,--defsym=ROM_SIZE=$(MONITOR_ROM_SIZE) -Wl,--defsym=ENTRYPOINT=$(MONITOR_ENTRYPOINT)
SRCS += h2700/hook.s ../monitor/monitor.c ../monitor/transport.c ../monitor/pcdrv.c
endif

include ../common.mk

../shell/shell_data.o: shell

# The executable is aligned to 32 bytes in order to make it easier for tools and
# homebrew apps to locate it by searching the ROM.
./psexe_data.o: $(EMBED_PSEXE)
	cp $(EMBED_PSEXE) psexe.bin
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=32 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips psexe.bin psexe_data.o
	rm -f psexe.bin

./font1.o: charset/font1.raw
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.font1,alloc,load,readonly,data,contents -O $(FORMAT) -B mips charset/font1.raw font1.o

./font2.o: charset/font2.raw
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.font2,alloc,load,readonly,data,contents -O $(FORMAT) -B mips charset/font2.raw font2.o

shell:
	$(MAKE) -C ../shell shell_data.o

deepclean: clean
	$(MAKE) -C ../shell clean

.PHONY: deepclean shell
