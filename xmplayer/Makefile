TARGET = xmplay-demo
TYPE = ps-exe

SRCS = \
../common/crt0/crt0.s \
../common/crt0/memory-c.c \
../common/crt0/memory-s.s \
../common/syscalls/printf.s \
demo.c \
xmplayer.c \
song.o \
samples.o \

include ../common.mk

song.o: song.xmp
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $< $@

samples.o: song.smp
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $< $@
