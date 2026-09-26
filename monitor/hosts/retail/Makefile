TARGET = monitor-retail
TYPE = ps-exe

SRCS = \
../../../common/crt0/crt0.s \
main.c \
../../monitor.c \
../../pcdrv.c \
../../transport.c \

CPPFLAGS += -DMONITOR_LINK_SIO1
# SIO1 baud reload at x16, see monitor/link-sio1.h.
MONITOR_SIO1_RELOAD ?= 18
CPPFLAGS += -DMONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)
# Linked high so targets keep the usual 0x80010000.
LDFLAGS += -Xlinker --defsym=TLOAD_ADDR=0x801c0000
LDFLAGS += -Xlinker --defsym=__globals=0x100 -Xlinker --defsym=__globals60=0x60

include ../../../common.mk
