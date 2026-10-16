TARGET = modplayer-upload-tail
TYPE = ps-exe

SRCS = \
modplayer-upload-tail.cpp \
modplayer-seq.c \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20

include ../../psyqo/psyqo.mk
