TARGET = alloc-tracker
TYPE = ps-exe

SRCS = \
alloc-tracker.cpp \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20

include ../../psyqo/psyqo.mk
