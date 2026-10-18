TARGET = adpcmenc-bench
TYPE = ps-exe
SRCS = \
../common/crt0/crt0.s \
../common/crt0/memory-c.c \
../common/crt0/memory-s.s \
bench.c \
adpcmenc.c \
s0.o s1.o s4.o \

include ../common.mk

%.o: %.raw
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $< $@
