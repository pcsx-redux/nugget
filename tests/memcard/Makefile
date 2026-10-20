TARGET = memcard
TYPE = ps-exe

SRCS = \
memcard.cpp \

ifeq ($(PCSX_TESTS),true)
CPPFLAGS = -DPCSX_TESTS=1
endif
CXXFLAGS = -std=c++20

include ../../psyqo/psyqo.mk
