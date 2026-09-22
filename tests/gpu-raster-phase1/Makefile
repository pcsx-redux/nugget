TARGET = gpu-raster-phase1
USE_FUNCTION_SECTIONS = false
TYPE = ps-exe

SRCS = \
../uC-sdk-glue/BoardConsole.c \
../uC-sdk-glue/BoardInit.c \
../uC-sdk-glue/init.c \
\
../../third_party/uC-sdk/libc/src/cxx-glue.c \
../../third_party/uC-sdk/libc/src/errno.c \
../../third_party/uC-sdk/libc/src/initfini.c \
../../third_party/uC-sdk/libc/src/malloc.c \
../../third_party/uC-sdk/libc/src/qsort.c \
../../third_party/uC-sdk/libc/src/rand.c \
../../third_party/uC-sdk/libc/src/reent.c \
../../third_party/uC-sdk/libc/src/stdio.c \
../../third_party/uC-sdk/libc/src/string.c \
../../third_party/uC-sdk/libc/src/strto.c \
../../third_party/uC-sdk/libc/src/unistd.c \
../../third_party/uC-sdk/libc/src/xprintf.c \
../../third_party/uC-sdk/libc/src/xscanf.c \
../../third_party/uC-sdk/libc/src/yscanf.c \
../../third_party/uC-sdk/os/src/devfs.c \
../../third_party/uC-sdk/os/src/filesystem.c \
../../third_party/uC-sdk/os/src/fio.c \
../../third_party/uC-sdk/os/src/hash-djb2.c \
../../third_party/uC-sdk/os/src/init.c \
../../third_party/uC-sdk/os/src/osdebug.c \
../../third_party/uC-sdk/os/src/romfs.c \
../../third_party/uC-sdk/os/src/sbrk.c \


CPPFLAGS = -DNOFLOATINGPOINT
CPPFLAGS += -I.
CPPFLAGS += -I../../third_party/uC-sdk/libc/include
CPPFLAGS += -I../../third_party/uC-sdk/os/include
CPPFLAGS += -I../../third_party/libcester/include
CPPFLAGS += -I../../openbios/uC-sdk-glue

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
../../common/syscalls/printf.s \
../../common/crt0/uC-sdk-crt0.s \
gpu-raster-phase1.c \

include ../../common.mk
