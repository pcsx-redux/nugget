TARGET = cdda-waterfall-spectrum
TYPE = ps-exe

SRCS = \
main.cpp \
../../../psyqo/examples/cdda-waterfall/spectrum.cpp \
test-spectrum.cpp \

CXXFLAGS = -std=c++20
CPPFLAGS += -I../../../psyqo/examples/cdda-waterfall
LDFLAGS += -Wl,--defsym=__stack_reserve=0x8000

include ../../../psyqo/snitch.mk
