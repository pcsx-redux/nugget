TARGET = lz4bench
TYPE = ps-exe

# PAYLOAD=<files to compress into the benchmark>; mkpayload.py needs the
# python lz4 module.
PAYLOAD ?= $(wildcard ../../../tests/cdrom/cdrom.ps-exe ../../../tests/gte/gte.ps-exe ../../../tests/psyqo/psyqo-tests.ps-exe)

SRCS = \
../../../common/crt0/crt0.s \
main.c \
payload.s \
../../lz4stream.c \
../../../common/syscalls/printf.s \

payload.bin: mkpayload.py $(PAYLOAD)
	python3 mkpayload.py $@ $(PAYLOAD)

payload.o: payload.bin

clean::
	rm -f payload.bin

include ../../../common.mk
