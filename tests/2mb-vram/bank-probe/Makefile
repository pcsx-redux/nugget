TARGET = bank-probe
TYPE = ps-exe

include ../printf.mk

SRCS += \
../../../common/crt0/memory-s.s \
../../../common/crt0/memory-c.c \
../../../common/crt0/crt0.s \
bank-probe.c \

CPPFLAGS = -I..

include ../../../common.mk
