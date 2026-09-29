# FT232H board presets for MONITOR_LINK=FT232H, one monitor build per board:
# the hardware has nowhere to store the link addresses. Select one with
# MONITOR_FT232H_BOARD, or pass MONITOR_FT232H_DATA / _STATUS / _EXP1_CONFIG
# directly. Addresses are KSEG1, see monitor/link-ft232h.h. Only orion has run
# on silicon.
#
#   psx232h-a20   psx232h with A0 on A20, EXP1 widened to 8 MB
#   psx232h-a0    psx232h with A0 on A0, in the comms window at 0x1f060000
#   picodev-usb   Pico-Dev, FT232H emulation on its USB channel
#   picodev-uart  Pico-Dev, same interface on its UART channel
#   piodev-lite   PIO-Dev-Lite, FT232H on the second 2 MB of /CS0 (derived
#                 from its netlist), EXP1 widened to 4 MB
#   orion         Orion's cart, an FTDI FIFO chip with A18 selecting status,
#                 both status bits active low (from psx232's support for it)

ifeq ($(MONITOR_FT232H_BOARD),psx232h-a20)
CPPFLAGS += -DMONITOR_FT232H_DATA=0xbf000000 -DMONITOR_FT232H_STATUS=0xbf100000
CPPFLAGS += '-DMONITOR_FT232H_EXP1_CONFIG=((23 << 16) | 0x2422)'
else ifeq ($(MONITOR_FT232H_BOARD),psx232h-a0)
CPPFLAGS += -DMONITOR_FT232H_DATA=0xbf060004 -DMONITOR_FT232H_STATUS=0xbf060005
else ifeq ($(MONITOR_FT232H_BOARD),picodev-usb)
CPPFLAGS += -DMONITOR_FT232H_DATA=0xbf000000 -DMONITOR_FT232H_STATUS=0xbf000001
else ifeq ($(MONITOR_FT232H_BOARD),picodev-uart)
CPPFLAGS += -DMONITOR_FT232H_DATA=0xbf000002 -DMONITOR_FT232H_STATUS=0xbf000003
else ifeq ($(MONITOR_FT232H_BOARD),piodev-lite)
CPPFLAGS += -DMONITOR_FT232H_DATA=0xbf200000 -DMONITOR_FT232H_STATUS=0xbf200001
CPPFLAGS += '-DMONITOR_FT232H_EXP1_CONFIG=((22 << 16) | 0x243f)'
else ifeq ($(MONITOR_FT232H_BOARD),orion)
CPPFLAGS += -DMONITOR_FT232H_DATA=0xbf020000 -DMONITOR_FT232H_STATUS=0xbf060000
CPPFLAGS += '-DMONITOR_FT232H_EXP1_CONFIG=((23 << 16) | 0x921)' -DMONITOR_FT232H_ACTIVE_LOW
else ifneq ($(MONITOR_FT232H_BOARD),)
$(error unknown MONITOR_FT232H_BOARD '$(MONITOR_FT232H_BOARD)', see monitor/hosts/ft232h-boards.mk)
endif
