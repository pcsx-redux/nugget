TARGET = h2700flash
TYPE = ps-exe

SRCS = \
../../../../common/crt0/crt0.s \
payload.s \
main/main.c \

# Above the RAM-linked monitor (0x80020000..) and its image; below the stack.
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=0x80100000
CPPFLAGS += -DMODE='"$(MODE)"' $(if $(GO),-DFLASH_GO)
MODE ?= DRY

include ../../../../common.mk

payload.o: image.bin

# STOCK is a dump of the stock H2700 flash; the monitor is ../../.. built
# with `make BOOT=cart MONITOR=1`.
image.bin: $(wildcard ../../../openbios.elf)
	@test -n "$<" || { echo "build ../../.. with 'make BOOT=cart MONITOR=1' first" >&2; exit 1; }
	@test -n "$(STOCK)" || { echo "set STOCK=<stock H2700 flash dump> to build image.bin" >&2; exit 1; }
	python3 mkimage.py $(STOCK) $< $@ $(PREFIX)-objcopy
