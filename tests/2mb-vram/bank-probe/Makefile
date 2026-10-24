TARGET = bank-probe
TYPE = ps-exe

SRCS += ../../../common/syscalls/printf.s

SRCS += \
../../../common/crt0/memory-s.s \
../../../common/crt0/memory-c.c \
../../../common/crt0/crt0.s \
bank-probe.c \

CPPFLAGS = -I..
ifeq ($(ATCONS),1)
CPPFLAGS += -DBANK_PROBE_ATCONS
endif

include ../../../common.mk
