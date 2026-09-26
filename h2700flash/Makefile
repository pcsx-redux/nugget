TARGET = h2700flash
TYPE = ps-exe

SRCS = \
../common/crt0/crt0.s \
payload.s \
main/main.c \

# Above the RAM-linked monitor (0x80020000..) and its image; below the stack.
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=0x80100000
CPPFLAGS += -DMODE='"$(MODE)"' $(if $(GO),-DFLASH_GO)
MODE ?= DRY

include ../common.mk
