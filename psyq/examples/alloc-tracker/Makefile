TARGET = alloc-tracker
TYPE = ps-exe

SRCS = alloc-tracker.c \
../../alloc-tracker/psyq-alloc-tracker.c \
../../../common/crt0/crt0.s \

CPPFLAGS += -I../../include
CPPFLAGS += -I../..
LDFLAGS += -L../../lib
LDFLAGS += -Wl,--start-group
LDFLAGS += -lapi
LDFLAGS += -lc
LDFLAGS += -lc2
LDFLAGS += -Wl,--end-group

include ../../../common.mk
