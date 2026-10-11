# `make SIO1=true` streams probe output over SIO1 (115200 8N2, RTS asserted)
# instead of the BIOS TTY, for loaders without TTY redirection.
ifeq ($(SIO1),true)
SRCS += ../sio1-printf.c
else
SRCS += ../../../common/syscalls/printf.s
endif
