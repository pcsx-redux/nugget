TARGET = cdrom-dir
TYPE = ps-exe

SRCS = \
cdrom-dir.cpp \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20 -fcoroutines

include ../../psyqo.mk
