TARGET = shello
TYPE = ps-exe

SRCS = \
../modplayer/modplayer.c \
blip.o \
cdcheck.cpp \
fixedmath.cpp \
hull.cpp \
shello.cpp \
sound.cpp \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CXXFLAGS = -std=c++20

# The OpenBIOS shell is copied to and entered at 0x80030000 by startShell().
# Match that load address so the standalone build mirrors the real deployment.
LDFLAGS += -Wl,--defsym,TLOAD_ADDR=0x80030000

include ../psyqo/psyqo.mk

# Same music as the C shell, so it reuses that copy. objcopy runs from the
# file's own directory so the symbol stays _binary_blip_hit_start.
blip.o: ../shell/blip.hit
	cd $(dir $<) && $(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $(notdir $<) $(CURDIR)/$@
