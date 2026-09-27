# Board the monitor runs on, for the hosts that can run on more than one.
# Select with MONITOR_PLATFORM:
#
#   retail   a PS1 (default)
#   sys573   Konami System 573: SIO1 RTS held up (RTS is looped back to CTS),
#            watchdog at 0x1f5c0000
#   gv       Konami GV: as sys573, watchdog at 0x1f780000
#
# See monitor/watchdog.h and monitor/link-sio1.h.

MONITOR_PLATFORM ?= retail

ifeq ($(MONITOR_PLATFORM),sys573)
CPPFLAGS += -DMONITOR_PLATFORM_SYS573 -DMONITOR_SIO1_RTS_ALWAYS
else ifeq ($(MONITOR_PLATFORM),gv)
CPPFLAGS += -DMONITOR_PLATFORM_GV -DMONITOR_SIO1_RTS_ALWAYS
else ifneq ($(MONITOR_PLATFORM),retail)
$(error unknown MONITOR_PLATFORM '$(MONITOR_PLATFORM)', see monitor/hosts/platforms.mk)
endif
