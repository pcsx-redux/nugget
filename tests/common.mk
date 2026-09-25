USE_FUNCTION_SECTIONS = false
TYPE = ps-exe

SRCS = \
../support/alloc.c \
../support/crt0.s \
../support/memory-c.c \
../support/memory-s.s \
../support/runtime.c \
../support/xprintf.c \
../../common/syscalls/printf.s \

CPPFLAGS = -I.
CPPFLAGS += -I../support/include
CPPFLAGS += -I../../third_party/libcester/include

ifeq ($(PCSX_TESTS),true)
CPPFLAGS += -DPCSX_TESTS=1
endif
