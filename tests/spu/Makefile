TARGET = spu

include ../common.mk

ifeq ($(SPU_DUMP),true)
CPPFLAGS += -DSPU_DUMP=1
endif

ifeq ($(SPU_ADPCM_EDGE_GOLDENS),true)
CPPFLAGS += -DSPU_ADPCM_EDGE_GOLDENS_AVAILABLE=1
endif

ifeq ($(SPU_REVERB_GOLDENS),true)
CPPFLAGS += -DSPU_REVERB_GOLDENS_AVAILABLE=1
endif

SRCS += \
spu.c \

include ../../common.mk

# generate .test.pcm samples instead of running assertion
.PHONY: spu_dump
spu_dump:
	$(MAKE) clean
	$(MAKE) SPU_DUMP=true

