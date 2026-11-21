TARGET = shello
TYPE = ps-exe

SRCS = \
crt0.s \
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
# Unpacked, link it there. Packed (PACK_SHELL=true, as for the cart), the
# packer's stub sits at 0x80030000 and unpacks the shell to the default
# 0x80010000, clear of the compressed data, like the C shell.
ifneq ($(PACK_SHELL),true)
LDFLAGS += -Wl,--defsym,TLOAD_ADDR=0x80030000
endif

include ../psyqo/psyqo.mk

# Same music as the C shell, so it reuses that copy. objcopy runs from the
# file's own directory so the symbol stays _binary_blip_hit_start.
blip.o: ../shell/blip.hit
	cd $(dir $<) && $(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $(notdir $<) $(CURDIR)/$@

# OpenBIOS embeds the shell as a raw image it copies to 0x80030000 and calls.
# The symbols are named after the C shell's, so openbios links either one.
ifeq ($(PACK_SHELL),true)
shello.bin: shello.ps-exe
	../../../ps1-packer shello.ps-exe -tload 0x80030000 -raw -o shello.bin
else
shello.bin: shello.elf
	$(PREFIX)-objcopy -O binary -R .PSX_EXE_Header $< $@
endif

shell_data.o: shello.bin
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  --redefine-sym _binary_shello_bin_start=_binary_shell_bin_start \
	  --redefine-sym _binary_shello_bin_end=_binary_shell_bin_end \
	  --redefine-sym _binary_shello_bin_size=_binary_shell_bin_size \
	  -O $(FORMAT) -B mips $< $@

clean::
	rm -f shello.bin shell_data.o
