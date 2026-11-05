TARGET = lz4

include ../common.mk

SRCS += \
lz4.c \
lz4-c.c \
../../lz4/lz4-mips.S \

CPPFLAGS += -I../.. -DLZ4_RANDOM_BLOCKS=200

include ../../common.mk
