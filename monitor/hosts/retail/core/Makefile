# The resident half of the retail monitor: everything the exception handler
# reaches, linked into the free RAM under the kernel's control blocks (see
# core.ld). The loader one directory up copies monitor-core.bin there.
TARGET = monitor-core
TYPE = bin
BUILD ?= LTO
LDSCRIPT = core.ld

SRCS = \
host-monitor.c \
host-transport.c \
host-tty.c \

MONITOR_LINK ?= SIO1
CPPFLAGS += -DMONITOR_LINK_$(MONITOR_LINK)
MONITOR_SIO1_RELOAD ?= 18
CPPFLAGS += -DMONITOR_SIO1_RELOAD=$(MONITOR_SIO1_RELOAD)
LDFLAGS += -Xlinker --defsym=__globals=0x100 -Xlinker --defsym=__globals60=0x60

.DEFAULT_GOAL := all
clean::
	rm -f $(TARGET).bin

include ../../../../common.mk
