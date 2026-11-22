TARGET = psyqo-tests
TYPE = ps-exe

SRCS = \
psyqo-tests.cpp \
test-fixed-point.cpp \
test-vector.cpp \
test-trigonometry.cpp \
test-soft-math.cpp \
test-msf.cpp \
test-adler32.cpp \
test-bezier.cpp \
test-primitives.cpp \
test-gte-math.cpp \
test-kernel-callbacks.cpp \
test-bump-allocator.cpp \
test-heap-limit.cpp \
test-tim.cpp \

CXXFLAGS = -std=c++20
LDFLAGS += -Wl,--defsym=__stack_reserve=0x8000

include ../../../psyqo/snitch.mk
