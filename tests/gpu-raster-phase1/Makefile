TARGET = gpu-raster-phase1

include ../common.mk

# Phase 1 binary is intentionally NOT gated on PCSX_TESTS - failures on
# Redux are the deliverable, not a regression to suppress.
#
# It IS run under Redux's test harness: tests/pcsxrunner/gpu-raster.cc
# instantiates GPU_RASTER_TEST(1) through (22), each booting the phase
# binary with -testmode -loadexe and asserting exit 0, so a cester failure
# in here surfaces as a red gtest and not only in a hardware capture.
# The comment here said the opposite from 2026-05-15 until now; the suite
# was wired into pcsx-redux-tests in eac53b72, ten hours after it landed.

SRCS += \
gpu-raster-phase1.c \

include ../../common.mk
