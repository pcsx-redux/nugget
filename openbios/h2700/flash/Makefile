TARGET = h2700flash
TYPE = ps-exe

SRCS = \
../../../common/crt0/crt0.s \
payload.s \
main/main.c \

# Above the RAM-linked monitor (0x80020000..) and its image; below the stack.
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=0x80100000
# Only GO=1 builds the erase/program path. main.o is rebuilt on every make so
# a GO build never leaves its object behind for a later DRY one.
ifeq ($(GO),1)
FLASH_MODE := GO
CPPFLAGS += -DFLASH_GO
else
FLASH_MODE := DRY
endif
CPPFLAGS += -DMODE='"$(FLASH_MODE)"'

include ../../../common.mk

payload.o: image.bin
main/main.o: FORCE
FORCE:
.PHONY: FORCE

# The payload is rebuilt on every make, from exactly one source, so a leftover
# image.bin is never flashed by accident:
#   STOCK=<stock H2700 flash dump>, with ../.. built as `make BOOT=cart MONITOR=1`
#   IMAGE=<ready 512 KB flash image>
ELF := $(wildcard ../../openbios.elf)
image.bin: FORCE
ifneq ($(STOCK),)
	@test -n "$(ELF)" || { echo "build ../.. with 'make BOOT=cart MONITOR=1' first" >&2; exit 1; }
	python3 mkimage.py $(STOCK) $(ELF) $@ $(PREFIX)-objcopy
else ifneq ($(IMAGE),)
	cp $(IMAGE) $@
else
	@echo "set STOCK=<stock H2700 flash dump> or IMAGE=<flash image>" >&2; exit 1
endif
