TARGET = keynote
TYPE = ps-exe

SRCS = \
../psmplayer/psmplayer.c \
keynote-psm.o \
keynote-vab.o \
keynote.cpp \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20

include ../psyqo/psyqo.mk

# keynote.psm and keynote.vab come from keynote.mid:
#   midi2psm keynote.mid -s TimGM6mb.sf2 -o keynote.psm -b keynote.vab
keynote-psm.o: keynote.psm
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $< $@

keynote-vab.o: keynote.vab
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $< $@
