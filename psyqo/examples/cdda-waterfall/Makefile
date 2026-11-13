TARGET = cdda-waterfall
TYPE = ps-exe

SRCS = \
cdda-waterfall.cpp \
spectrum.cpp \
spu-capture.cpp \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20

include ../../psyqo.mk
