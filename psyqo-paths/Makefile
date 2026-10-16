PSYQOPATHSDIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

TARGET = psyqo-paths
TYPE = library

SRCS = \
src/cdrom-loader.cpp \
src/archive-manager.cpp \
src/str-player.cpp \
../bsdec/bsdec.c \
../common/crt0/clz.c \
../ucl-demo/n2e-d.S \

CPPFLAGS += -I..

# bsdec's AC loop is most of a frame's CPU time and -Os is the wrong knob for it.
../bsdec/bsdec.o: CPPFLAGS += -O3

EXTRA_DEPS += $(PSYQOPATHSDIR)Makefile

include ../psyqo/psyqo.mk
