TARGET = cartflash
TYPE = ps-exe

SRCS = \
../../../common/crt0/crt0.s \
payload.s \
main.c \

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
main.o: FORCE
FORCE:
.PHONY: FORCE

# The payload is copied on every make from IMAGE=<cart image, at most 128 KB>
# (e.g. ../../hosts/cart/monitor-cart.rom), so a leftover image.bin is never
# flashed by accident.
image.bin: FORCE
ifneq ($(IMAGE),)
	cp '$(IMAGE)' '$@'
else
	@echo "set IMAGE=<cart image>" >&2; exit 1
endif

clean::
	rm -f image.bin
