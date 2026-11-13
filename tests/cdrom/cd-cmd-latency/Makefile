TARGET = cd-cmd-latency
TYPE = ps-exe
USE_FUNCTION_SECTIONS = false

SRCS = \
../../runtime/alloc.c \
../../runtime/crt0.s \
../../runtime/memory-c.c \
../../runtime/memory-s.s \
../../runtime/runtime.c \
../../runtime/xprintf.c \
../../../common/syscalls/printf.s \
cd-cmd-latency.c \

CPPFLAGS = -I.
CPPFLAGS += -I../../runtime/include

include ../../../common.mk
