TARGET = strplayer
TYPE = ps-exe

SRCS = \
strplayer.cpp \

# The movie to play, as an ISO9660 path on the disc in the drive.
MOVIE ?= MOVIE.STR;1
# PRESENT=asap switches the display the moment a frame is decoded, instead of
# at the vblank after the next frame arrived. RGB15=1 decodes to 15 bits.
PRESENT ?=
RGB15 ?=

CPPFLAGS += -DSTRPLAYER_MOVIE='"$(MOVIE)"'
ifeq ($(PRESENT),asap)
CPPFLAGS += -DSTRPLAYER_ASAP
endif
ifneq ($(STOPAT),)
CPPFLAGS += -DSTRPLAYER_STOPAT=$(STOPAT)
endif
ifneq ($(SLOTS),)
CPPFLAGS += -DSTRPLAYER_SLOTS=$(SLOTS)
endif
ifneq ($(RGB15),)
CPPFLAGS += -DSTRPLAYER_RGB15
endif
ifeq ($(TEST),true)
CPPFLAGS += -Werror
endif
CXXFLAGS = -std=c++20

include ../../psyqo-paths.mk
