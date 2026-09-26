TARGET = hello-probe
TYPE = ps-exe

SRCS = \
crt0-probe.s \
probe.c \
../transport.c \

# -I../../.. points at src/mips so "openbios/monitor/transport.h",
# "common/hardware/hwregs.h" and "common/hardware/util.h" resolve.
CPPFLAGS += -I../../..
# -G0: no small-data / $gp-relative addressing, so the bare crt0 needs no $gp.
CFLAGS += -G0
# Load/link the probe into main RAM instead of the flash cave.
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=0x80100000

include ../../../common.mk
