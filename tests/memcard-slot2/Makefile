TARGET = memcard-slot2
TYPE = ps-exe

SRCS = \
memcard-slot2.cpp \

ifeq ($(PCSX_TESTS),true)
CPPFLAGS = -DPCSX_TESTS=1
endif
CXXFLAGS = -std=c++20

include ../../psyqo/psyqo.mk
