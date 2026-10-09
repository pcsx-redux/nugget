TARGET = spu-upload-tail
TYPE = ps-exe

SRCS = \
spu-upload-tail.cpp \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20

include ../../psyqo/psyqo.mk
