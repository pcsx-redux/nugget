TARGET = gpu-raster-phase2

include ../common.mk

# Pull raster-helpers.h from phase-1 (the canonical shared-header home).
CPPFLAGS += -I../gpu-raster-phase1

# Stand-alone build (no PCSX_TESTS gating). See gpu-raster-phase1/Makefile.

SRCS += \
gpu-raster-phase2.c \

include ../../common.mk
