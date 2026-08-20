TARGET = gte-math-bench
TYPE = ps-exe
USE_FUNCTION_SECTIONS = false

SRCS = \
gte-math-bench.cpp \

CXXFLAGS = -std=c++20

ifeq ($(PCSX_TESTS),true)
CPPFLAGS += -DPCSX_TESTS=1
endif

include ../../psyqo/psyqo.mk
