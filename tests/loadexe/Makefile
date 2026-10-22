TARGET = loadexe

include ../common.mk

# at30000.s has to come first so its code is the very first thing in the binary.
SRCS := at30000.s $(SRCS)
SRCS += loadexe.c

LDFLAGS += -Wl,--defsym,TLOAD_ADDR=0x80030000

include ../../common.mk
