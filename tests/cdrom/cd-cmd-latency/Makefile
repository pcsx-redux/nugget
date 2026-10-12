TARGET = cd-cmd-latency
TYPE = ps-exe
USE_FUNCTION_SECTIONS = false

SRCS = \
../../support/alloc.c \
../../support/crt0.s \
../../support/memory-c.c \
../../support/memory-s.s \
../../support/runtime.c \
../../support/xprintf.c \
../../../common/syscalls/printf.s \
cd-cmd-latency.c \

CPPFLAGS = -I.
CPPFLAGS += -I../../support/include

include ../../../common.mk
