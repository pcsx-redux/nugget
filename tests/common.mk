USE_FUNCTION_SECTIONS = false
TYPE = ps-exe

# Paths below are relative to the test's own directory, at whatever depth it sits.
TESTS_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

SRCS = \
$(TESTS_DIR)/runtime/alloc.c \
$(TESTS_DIR)/runtime/crt0.s \
$(TESTS_DIR)/runtime/memory-c.c \
$(TESTS_DIR)/runtime/memory-s.s \
$(TESTS_DIR)/runtime/runtime.c \
$(TESTS_DIR)/runtime/xprintf.c \
$(TESTS_DIR)/../common/syscalls/printf.s \

CPPFLAGS = -I.
CPPFLAGS += -I$(TESTS_DIR)/runtime/include
CPPFLAGS += -I$(TESTS_DIR)/../third_party/libcester/include

ifeq ($(PCSX_TESTS),true)
CPPFLAGS += -DPCSX_TESTS=1
endif
