TARGET = irqcost
TYPE = ps-exe

SRCS = \
irqcost.cpp \
measure.s \

LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=0x80001000

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20

include ../../psyqo/psyqo.mk
