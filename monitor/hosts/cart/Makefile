TARGET = monitor-cart
TYPE = ps-exe

SRCS = \
../../../common/crt0/crt0.s \
main.c \
host-monitor.c \
host-pcdrv.c \
host-transport.c \
host-tty.c \

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

clean::
	rm -f rom.o $(TARGET).rom.elf $(TARGET).rom

include ../../../common.mk
