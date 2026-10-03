TARGET = c64
TYPE = ps-exe

SRCS = \
c64.cpp \
c64jit.cpp \
c64main.cpp \

CPPFLAGS += -I../../..
CXXFLAGS = -std=c++20
CPPFLAGS += -O3 $(EXTRA)

# The ROMs are MEGA65 Open ROMs (LGPL-3), fetched at build time rather than
# kept in this tree. Pinned to a commit so the build is reproducible.
OPENROMS_URL = https://raw.githubusercontent.com/MEGA65/open-roms/f9a1bab1fddc99aeb872ca7c15dac71197c86ac7/bin

include ../../psyqo.mk

basic_generic.rom kernal_generic.rom chargen_openroms.rom:
	curl -sSLf -o $@ $(OPENROMS_URL)/$@

roms.h: basic_generic.rom kernal_generic.rom chargen_openroms.rom
	( xxd -i -n c_basic basic_generic.rom; xxd -i -n c_kernal kernal_generic.rom; \
	  xxd -i -n c_chargen chargen_openroms.rom ) | sed 's/^unsigned char/static const unsigned char/; /_len =/d' > $@

c64main.o c64main.dep: roms.h
