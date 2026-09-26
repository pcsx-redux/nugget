TARGET = monitor-retail
TYPE = ps-exe
# The resident part has to fit a code cave, so size wins by default.
BUILD ?= LTO

SRCS = \
../../../common/crt0/crt0.s \
main.c \
host-tty.c \

# The resident half, built in core/ and copied into the cave at start-up.
OBJS += monitor-core-blob.o
EXTRA_DEPS += core/monitor-core.elf
LDFLAGS += -Wl,--just-symbols=core/monitor-core.elf
CPPFLAGS += -DSIO1_TTY_INSTALL_ONLY

# SIO1, or FT232H for an FT232H on the expansion port (monitor/link-ft232h.h).
MONITOR_LINK ?= SIO1
CPPFLAGS += -DMONITOR_LINK_$(MONITOR_LINK)
# SIO1 baud reload at x16, see monitor/link-sio1.h.
MONITOR_SIO1_RELOAD ?= 18
CPPFLAGS += -DMONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)
# Linked high so targets keep the usual 0x80010000.
# MONITOR_TLOAD moves it, e.g. to load one monitor from another.
MONITOR_TLOAD ?= 0x801c0000
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=$(MONITOR_TLOAD)
LDFLAGS += -Xlinker --defsym=__globals=0x100 -Xlinker --defsym=__globals60=0x60

core/monitor-core.bin: core/monitor-core.elf
core/monitor-core.elf: FORCE
	$(MAKE) -C core MONITOR_LINK=$(MONITOR_LINK) MONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)

monitor-core-blob.o: core/monitor-core.bin
	cd core && $(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips monitor-core.bin ../$@

clean::
	$(MAKE) -C core clean
	rm -f monitor-core-blob.o

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
