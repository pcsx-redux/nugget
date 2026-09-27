TARGET = monitor-cart
TYPE = ps-exe
# The resident part has to fit a code cave, so size wins by default.
BUILD ?= LTO

SRCS = \
../../../common/crt0/crt0.s \
main.c \
host-tty.c \
../../../openbios/main/splash.c \

# The loader shows OpenBIOS's colour bars before installing the monitor.
CPPFLAGS += -DOPENBIOS_SHOW_SPLASH_SCREEN

# The resident half is the retail host's core, copied into the cave by main.c.
CORE = ../retail/core
OBJS += monitor-core-blob.o
EXTRA_DEPS += $(CORE)/monitor-core.elf
LDFLAGS += -Wl,--just-symbols=$(CORE)/monitor-core.elf
CPPFLAGS += -DSIO1_TTY_INSTALL_ONLY

CPPFLAGS += -DMONITOR_LINK_SIO1
MONITOR_SIO1_RELOAD ?= 18
CPPFLAGS += -DMONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=0x801c0000
LDFLAGS += -Xlinker --defsym=__globals=0x100 -Xlinker --defsym=__globals60=0x60

# The cart image: rom.s wraps the PS-EXE built above, linked at 0x1f000000.
all: $(TARGET).rom

$(TARGET).rom: rom.s $(TARGET).ps-exe
	$(CC) $(ARCHFLAGS) -c -o rom.o -Wa,-I. -DPSEXE=\"$(TARGET).ps-exe\" -x assembler-with-cpp rom.s
	$(PREFIX)-ld -EL -Ttext=0x1f000000 -e 0x1f000000 -o $(TARGET).rom.elf rom.o
	$(PREFIX)-objcopy -O binary -j .text $(TARGET).rom.elf $@

$(CORE)/monitor-core.bin: $(CORE)/monitor-core.elf
$(CORE)/monitor-core.elf: FORCE
	$(MAKE) -C $(CORE) MONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)

monitor-core-blob.o: $(CORE)/monitor-core.bin
	cd $(CORE) && $(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips monitor-core.bin $(CURDIR)/$@

clean::
	rm -f rom.o $(TARGET).rom.elf $(TARGET).rom monitor-core-blob.o
	$(MAKE) -C $(CORE) clean

FORCE:
.PHONY: FORCE

include ../../../common.mk
