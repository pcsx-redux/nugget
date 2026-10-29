# CPPFLAGS and LDFLAGS from the environment are exported to sub-makes along
# with everything appended below, which would build the core with the loader's
# defines. Keep what came from the environment and hand only that to the core.
CORE_CPPFLAGS := $(CPPFLAGS)
CORE_LDFLAGS := $(LDFLAGS)

TARGET = monitor-retail
TYPE = ps-exe
# The resident part has to fit a code cave, so size wins by default.
BUILD ?= LTO

SRCS = \
../../../common/crt0/crt0.s \
../../../common/crt0/memory-c.c \
../../../common/syscalls/printf.s \
main.c \
host-tty.c \
../splash.c \
../openbios-splash.c \

# The loader shows OpenBIOS's colour bars before installing the monitor.
CPPFLAGS += -DOPENBIOS_SHOW_SPLASH_SCREEN

# The resident half, built in core/ and copied into the cave at start-up.
OBJS += monitor-core-blob.o
EXTRA_DEPS += monitor-core-syms.elf
# The core's BIOS call thunks stay out of the loader's link: it emits its own,
# and the core's copies sit in the cave, out of reach of a j from the loader.
LDFLAGS += -Wl,--just-symbols=monitor-core-syms.elf
CPPFLAGS += -DSIO1_TTY_INSTALL_ONLY
# MONITOR_NO_SLOT=1 leaves the kernel exception handler's patch slot alone:
# the monitor rides the priority-0 chain only (monitor/install.h).
ifneq ($(MONITOR_NO_SLOT),)
CPPFLAGS += -DMONITOR_NO_SLOT
endif

# SIO1, or FT232H for an FT232H on the expansion port (monitor/link-ft232h.h).
MONITOR_LINK ?= SIO1
CPPFLAGS += -DMONITOR_LINK_$(MONITOR_LINK)
include ../ft232h-boards.mk
include ../platforms.mk
# crt0 sets up the 573's DEV0 and clears its watchdog before anything else.
ifeq ($(MONITOR_PLATFORM),sys573)
ASFLAGS += -Wa,--defsym,MONITOR_PLATFORM_SYS573=1
endif
# SIO1 baud reload at x16, see monitor/link-sio1.h.
MONITOR_SIO1_RELOAD ?= 18
CPPFLAGS += -DMONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)
# Linked high so targets keep the usual 0x80010000.
# MONITOR_TLOAD moves it, e.g. to load one monitor from another.
MONITOR_TLOAD ?= 0x801c0000
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=$(MONITOR_TLOAD)
LDFLAGS += -Xlinker --defsym=__globals=0x100 -Xlinker --defsym=__globals60=0x60

core/monitor-core.bin: core/monitor-core.elf
monitor-core-syms.elf: core/monitor-core.elf
	$(PREFIX)-objcopy --wildcard --strip-symbol="__nugget_bios_*" core/monitor-core.elf $@
core/monitor-core.elf: export CPPFLAGS := $(CORE_CPPFLAGS)
core/monitor-core.elf: export LDFLAGS := $(CORE_LDFLAGS)
core/monitor-core.elf: FORCE
	$(MAKE) -C core MONITOR_LINK=$(MONITOR_LINK) MONITOR_PLATFORM=$(MONITOR_PLATFORM) MONITOR_FT232H_BOARD=$(MONITOR_FT232H_BOARD) MONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)

monitor-core-blob.o: core/monitor-core.bin
	cd core && $(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips monitor-core.bin ../$@

clean::
	$(MAKE) -C core clean
	rm -f monitor-core-blob.o monitor-core-syms.elf

FORCE:
.PHONY: FORCE

# A bootable disc image of the same EXE: `make iso`. exe2iso is built with
# pcsx-redux (tools/exe2iso); the disc holds PSX.EXE and no SYSTEM.CNF, which
# the BIOS boots by default. No license sectors, so it needs a console that
# boots burned discs.
EXE2ISO ?= exe2iso
.DEFAULT_GOAL := all
iso: $(TARGET).cue

$(TARGET).cue: $(TARGET).ps-exe
	$(EXE2ISO) $< -o $(TARGET).bin
	printf 'FILE "$(TARGET).bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' > $@

clean::
	rm -f $(TARGET).bin $(TARGET).cue

.PHONY: iso

include ../../../common.mk
