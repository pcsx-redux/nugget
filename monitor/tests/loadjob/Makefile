TARGET = loadjob
TYPE = ps-exe

# PAYLOAD=<file carried in the image>, copied in on every make so a stale
# payload.bin is never the one measured.
PAYLOAD ?= main.c

SRCS = \
../../../common/crt0/crt0.s \
main.c \
payload.s \

payload.bin: FORCE
	cp '$(PAYLOAD)' '$@'

payload.o: payload.bin

.PHONY: FORCE

clean::
	rm -f payload.bin

include ../../../common.mk
